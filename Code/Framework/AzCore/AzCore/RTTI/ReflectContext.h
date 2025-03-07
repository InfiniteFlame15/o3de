/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/RTTI/RTTI.h>

// For attributes
#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/deque.h>
#include <AzCore/std/smart_ptr/weak_ptr.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzCore/std/typetraits/decay.h>
#include <AzCore/std/typetraits/function_traits.h>
#include <AzCore/std/typetraits/has_member_function.h>
#include <AzCore/std/typetraits/is_function.h>
#include <AzCore/std/typetraits/is_member_function_pointer.h>
#include <AzCore/std/typetraits/is_same.h>
#include <AzCore/DOM/DomValue.h>
#include <AzCore/DOM/DomUtils.h>

namespace AZ
{
    class ReflectContext;

    class Attribute;

    /// Function type to called for on demand reflection within methods, properties, etc.
    /// OnDemandReflection functions must be static, so we can optimize by just using function pointer instead of object
    using StaticReflectionFunctionPtr = void(*)(ReflectContext* context);

    /// Must be function object, as these are typically virtual member functions (see ComponentDescriptor::Reflect)
    using ReflectionFunction = AZStd::function<void(ReflectContext* context)>;

    namespace Internal
    {
        // This needs to be not a pointer, because we're jamming it into shared_ptr, and we don't want a pointer to a pointer
        // #MSVC2013 does not allow using of a function signature.
        typedef void ReflectionFunctionRef(ReflectContext* context);
    }

    /**
     * Base classes for structures that store references to OnDemandReflection instantiations.
     * ReflectContext will own weak pointers to the function, so that we may look up already registered types.
     */
    class OnDemandReflectionOwner
    {
    public:
        virtual ~OnDemandReflectionOwner();

        // Disallow all copy-move operations, as well as default construct
        OnDemandReflectionOwner() = delete;
        OnDemandReflectionOwner(const OnDemandReflectionOwner&) = delete;
        OnDemandReflectionOwner& operator=(const OnDemandReflectionOwner&) = delete;
        OnDemandReflectionOwner(OnDemandReflectionOwner&&) = delete;
        OnDemandReflectionOwner& operator=(OnDemandReflectionOwner&&) = delete;

        /// Register an OnDemandReflection function
        void AddReflectFunction(AZ::Uuid typeId, StaticReflectionFunctionPtr reflectFunction);

    protected:
        /// Constructor to be called by child class
        explicit OnDemandReflectionOwner(ReflectContext& context);

    private:
        /// Reflection functions this instance owns references to
        AZStd::vector<AZStd::shared_ptr<Internal::ReflectionFunctionRef>> m_reflectFunctions;
        ReflectContext& m_reflectContext;
    };

    /**
     *  This is the default implementation for OnDemandReflection (no reflection). You can specialize this template class
     * for the type you want to reflect on demand.
     * \note Currently only \ref BehaviorContext support on demand reflection.
     */
    template<class T>
    struct OnDemandReflection
    {
        void NoSpecializationFunction();
    };

    AZ_HAS_MEMBER(NoOnDemandReflection, NoSpecializationFunction, void, ());

    /**
     * Base class for all reflection contexts.
     * Currently we recommend to follow the following declarative format for all context.
     * (Keep in mind this if only for direct C++ interface, code generators will generate code
     * on the top of this interface)
     * context->Class<TYPE>("Other descriptive parameters")
     *     ->Field/Propety("field name",&TYPE::m_field)
     *     ->Method("method name),&TYPE::Method)
     *     ...
     * We recommend that all ReflectContext implement a path to remove reflection when a reflected class/descriptor is deleted.
     * To do so make sure that when \ref ReflectContext::m_isRemoveReflection is set to true any calls to Class<...>() actualy remove
     * reflection. We recommend this approach so the user can write only one reflection function (not Reflect and "Unreflect")
     */
    class ReflectContext
    {
    public:
        AZ_RTTI(ReflectContext, "{B18D903B-7FAD-4A53-918A-3967B3198224}");

        ReflectContext();
        virtual ~ReflectContext() = default;

        void EnableRemoveReflection();
        void DisableRemoveReflection();
        bool IsRemovingReflection() const;

        /// Check if an OnDemandReflection type's typeid is already reflected
        bool IsOnDemandTypeReflected(AZ::Uuid typeId);

        // Derived classes should override this if they want users to be able to query whether something has already been reflected or not
        virtual bool IsTypeReflected(AZ::Uuid /*typeId*/) const { return false; }

        /// Execute all queued OnDemandReflection calls
        void ExecuteQueuedOnDemandReflections();

    protected:
        /// True if all calls in the context should be considered to remove not to add reflection.
        bool m_isRemoveReflection;

        /// Store the on demand reflect functions so we can avoid double-reflecting something
        AZStd::unordered_map<AZ::Uuid, AZStd::weak_ptr<Internal::ReflectionFunctionRef>> m_onDemandReflection;
        /// OnDemandReflection functions that need to be called
        AZStd::vector<AZStd::pair<AZ::Uuid, StaticReflectionFunctionPtr>> m_toProcessOnDemandReflection;
        /// The type ids of the currently reflecting type. Used to prevent circular references. Is a set to prevent recursive circular references.
        AZStd::deque<AZ::Uuid> m_currentlyProcessingTypeIds;

        friend OnDemandReflectionOwner;
    };
}

namespace AZ::Internal
{
    template<class RetType, class... Args>
    struct ClassToVoidInvoker;

    // Specialization for function object with no parameters
    template<class RetType>
    struct ClassToVoidInvoker<RetType>
    {
        explicit ClassToVoidInvoker(AZStd::function<RetType()> callable)
            : m_callable(AZStd::move(callable))
        {}

        // In the case of the function object not accepting at least one argument, then the class type is assumed to be non-existent
        RetType operator()() const
        {
            return m_callable();
        }

    private:
        AZStd::function<RetType()> m_callable;
    };

    // Specialization for function object with at least one parameter
    // The first parameter is assumed to be the class type
    // The void pointer instance will be cast to that type
    template<class RetType, class ClassType, class... Args>
    struct ClassToVoidInvoker<RetType, ClassType, Args...>
    {
        explicit ClassToVoidInvoker(AZStd::function<RetType(ClassType, Args...)> callable)
            : m_callable(AZStd::move(callable))
        {}
        RetType operator()(void* instancePtr, Args... args) const
        {
            if constexpr (AZStd::is_pointer_v<ClassType>)
            {
                // ClassType is a pointer so cast directly from the void pointer
                return m_callable(static_cast<ClassType>(instancePtr), static_cast<Args&&>(args)...);
            }
            else
            {
                // If the ClassType parameter accepts a value type, then cast the void pointer to a class type pointer
                // and deference
                return m_callable(*static_cast<AZStd::remove_cvref_t<ClassType>*>(instancePtr), static_cast<Args&&>(args)...);
            }
        }

    private:
        AZStd::function<RetType(ClassType, Args...)> m_callable;
    };

    template<class RetType>
    struct FunctionObjectInvoker
    {
        template<class... Args>
        using ClassToVoidArgsInvoker = ClassToVoidInvoker<RetType, Args...>;
    };

    // Custom struct to use as a unique_ptr deleter which can selectively deletes an attribute if
    // the caller should the pointer
    struct AttributeDeleter
    {
        AttributeDeleter();
        AttributeDeleter(bool deletePtr);

        void operator()(AZ::Attribute* attribute);

    private:
        bool m_deletePtr{ true };
    };
} // namespace AZ::Internal

namespace AZ
{
    using AttributeUniquePtr = AZStd::unique_ptr<AZ::Attribute, Internal::AttributeDeleter>;
    // Attributes to be used by reflection contexts

    /**
    * Base abstract class for all attributes. Use azrtti to get the
    * appropriate version. Of course if NULL there is a data mismatch of attributes.
    */
    class Attribute
    {
    public:
        using ContextDeleter = void(*)(void* contextData);

        static const AZ::Name s_typeField;
        static constexpr AZStd::string_view s_typeName = "AZ::Attribute";
        static const AZ::Name s_instanceField;
        static const AZ::Name s_attributeField;

        AZ_RTTI(AZ::Attribute, "{2C656E00-12B0-476E-9225-5835B92209CC}");
        Attribute()
            : m_contextData(nullptr, &DefaultDelete)
        { }
        virtual ~Attribute() = default;

        void SetContextData(void* contextData, ContextDeleter destroyer)
        {
            m_contextData = AZStd::unique_ptr<void, ContextDeleter>(contextData, destroyer);
        }

        void* GetContextData() const
        {
            return m_contextData.get();
        }

        /// Returns true if this attribute is an invokable function or method.
        virtual bool IsInvokable() const
        {
            return false;
        }

        virtual AttributeUniquePtr GetVoidInstanceAttributeInvocable()
        {
            constexpr bool deleteAttribute = false;
            return AttributeUniquePtr(this, Internal::AttributeDeleter{ deleteAttribute });
        }

        /// Returns true if this attribute is invokable, given a set of arguments.
        /// @param arguments A Dom::Value that must contain an Array of arguments for this invokable attribute.
        virtual bool CanDomInvoke([[maybe_unused]] const AZ::Dom::Value& arguments) const
        {
            return false;
        }

        /// Attempts to execute this attribute given an array of Dom::Values as parameters.
        /// @param arguments A Dom::Value that must contain an Array of arguments for this invokable attribute.
        /// @return A Dom::Value containing the marshalled result of the function call (null if the call returned void)
        virtual AZ::Dom::Value DomInvoke([[maybe_unused]] void* instance, [[maybe_unused]] const AZ::Dom::Value& arguments)
        {
            return {};
        }

        /// Gets a marshaleld Dom::Value representation of this attribute bound to a given instance.
        /// By default this just serializes a pointer to the instance and this attribute, but for non-invokable
        /// attributes this is just abbreviated to a marshalled version of the data stored in the attribute.
        virtual AZ::Dom::Value GetAsDomValue([[maybe_unused]] void* instance)
        {
            AZ::Dom::Value result(AZ::Dom::Type::Object);
            result[s_typeField] = Dom::Value(s_typeName, false);
            result[s_instanceField] = AZ::Dom::Utils::ValueFromType(instance);
            result[s_attributeField] = AZ::Dom::Utils::ValueFromType(this);
            return result;
        }

        bool m_describesChildren = false;
        bool m_childClassOwned = false;

    private:
        AZStd::unique_ptr<void, ContextDeleter> m_contextData; ///< a generic value you can use to store extra data associated with the attribute

        static void DefaultDelete(void*) { }
    };

    typedef AZ::u32 AttributeId;
    typedef AZStd::pair<AttributeId, Attribute*> AttributePair;
    typedef AZStd::vector<AttributePair> AttributeArray;

    template<typename ContainerType>
    inline Attribute* FindAttribute(AttributeId id, const ContainerType& attrArray)
    {
        for (const auto& attrPair : attrArray)
        {
            if (attrPair.first == id)
            {
                return attrPair.second ? &*attrPair.second : nullptr;
            }
        }
        return nullptr;
    }


    /**
    * Generic attribute by value data container. This is the most common attribute.
    */
    template<class T>
    class AttributeData
        : public Attribute
    {
    public:
        AZ_RTTI((AttributeData<T>, "{24248937-86FB-406C-8DD5-023B10BD0B60}", T), Attribute);
        AZ_CLASS_ALLOCATOR(AttributeData<T>, SystemAllocator, 0);
        template<class U>
        explicit AttributeData(U&& data)
            : m_data(AZStd::forward<U>(data)) {}
        virtual const T& Get(const void* instance) const { (void)instance; return m_data; }
        T& operator = (T& data) { m_data = data; return m_data; }
        T& operator = (const T& data) { m_data = data; return m_data; }

        AZ::Dom::Value GetAsDomValue(void*) override
        {
            if constexpr (AZStd::is_copy_constructible_v<T>)
            {
                return AZ::Dom::Utils::ValueFromType(m_data);
            }
            else
            {
                return AZ::Dom::Utils::ValueFromType(&m_data);
            }
        }
    private:
        T   m_data;
    };

    extern template class AttributeData<Crc32>;

    /**
    * Generic attribute for class member data, we use the object instance to access member data.
    * In general since the AttributeData::Get function already takes the instance there is no reason to cast (azrtti)
    * to this class (unless you really want to check/know).
    */
    template<class T>
    class AttributeMemberData;

    template<class T, class C>
    class AttributeMemberData<T C::*>
        : public AttributeData<T>
    {
    public:
        AZ_RTTI((AZ::AttributeMemberData<T C::*>, "{00E5F991-6B96-43CC-9869-F371548581D9}", T, C), AttributeData<T>);
        AZ_CLASS_ALLOCATOR(AttributeMemberData<T C::*>, SystemAllocator, 0);
        typedef T C::* DataPtr;
        explicit AttributeMemberData(DataPtr p)
            : AttributeData<T>(T())
            , m_dataPtr(p) {}
        const T& Get(const void* instance) const override { return (reinterpret_cast<const C*>(instance)->*m_dataPtr); }
        DataPtr GetMemberDataPtr() const { return m_dataPtr; }

        AZ::Dom::Value GetAsDomValue(void* instance) override
        {
            return AZ::Dom::Utils::ValueFromType(Get(instance));
        }
    private:
        DataPtr m_dataPtr;
    };

    template <typename Arg>
    bool CanInvokeFromDomArrayEntry(const AZ::Dom::Value& domArray, size_t index)
    {
        // Args must be packed as an array
        if (!domArray.IsArray())
        {
            return false;
        }

        // Unneeded arguments will be discarded, so larger array sizes are OK
        if (index >= domArray.ArraySize())
        {
            return true;
        }

        // Args should be convertible to our parameter type
        const Dom::Value& entry = domArray[index];
        if (!AZ::Dom::Utils::CanConvertValueToType<Arg>(entry))
        {
            // If it's not a safe conversion but it's a pointer type and we've got a void*, allow it
            if constexpr (AZStd::is_pointer_v<Arg>)
            {
                return entry.IsOpaqueValue() && entry.GetOpaqueValue().is<void*>();
            }
            // Otherwise, this is not a valid call
            else
            {
                return false;
            }
        }

        return true;
    }

    template <typename... T>
    bool CanInvokeFromDomArray(const AZ::Dom::Value& domArray)
    {
        size_t index = 0;
        if (!domArray.IsArray() || domArray.ArraySize() < sizeof...(T))
        {
            return false;
        }
        return (CanInvokeFromDomArrayEntry<T>(domArray, index++) && ...);
    }

    template <typename R, typename... Args, size_t... Is>
    Dom::Value InvokeFromDomArrayInternal(
        const AZStd::function<R(Args...)>& invokeFunction, const AZ::Dom::Value& domArray, AZStd::index_sequence<Is...>)
    {
        if constexpr (AZStd::is_same_v<R, void>)
        {
            invokeFunction(AZ::Dom::Utils::ValueToTypeUnsafe<Args>(domArray[Is])...);
            return {};
        }
        else
        {
            return AZ::Dom::Utils::ValueFromType(invokeFunction(AZ::Dom::Utils::ValueToTypeUnsafe<Args>(domArray[Is])...));
        }
    }

    template <typename R, typename... Args>
    Dom::Value InvokeFromDomArray(const AZStd::function<R(Args...)>& invokeFunction, const AZ::Dom::Value& domArray)
    {
        if (!domArray.IsArray() || domArray.ArraySize() < sizeof...(Args))
        {
            return {};
        }

        auto indexSequence = AZStd::make_index_sequence<sizeof...(Args)>{};
        return InvokeFromDomArrayInternal<R, Args...>(invokeFunction, domArray, indexSequence);
    }

    template <typename T>
    struct DomInvokeHelper
    {
        static bool CanInvoke(const AZ::Dom::Value&)
        {
            return false;
        }

        static AZ::Dom::Value Invoke(const T&, const AZ::Dom::Value&)
        {
            return {};
        }
    };

    template <typename R, typename... Args>
    struct DomInvokeHelper<AZStd::function<R(Args...)>>
    {
        static bool CanInvoke(const AZ::Dom::Value& args)
        {
            return CanInvokeFromDomArray<Args...>(args);
        }

        static AZ::Dom::Value Invoke(const AZStd::function<R(Args...)>& invokeFunction, const AZ::Dom::Value& args)
        {
            return InvokeFromDomArray<R, Args...>(invokeFunction, args);
        }
    };

    /**
    * Generic attribute global function pointer container. All function must return non void result (we can implement this)
    * as this is attribute and assumed to return something.
    */
    template<class F>
    class AttributeFunction;

    template<class R, class... Args>
    class AttributeFunction<R(Args...)> : public Attribute
    {
    public:
        AZ_RTTI((AZ::AttributeFunction<R(Args...)>, "{EE535A42-940C-42DE-848D-9C6CE57D8A62}", R, Args...), Attribute);
        AZ_CLASS_ALLOCATOR(AttributeFunction<R(Args...)>, SystemAllocator, 0);
        typedef R(*FunctionPtr)(Args...);
        explicit AttributeFunction(FunctionPtr f)
                : m_function(f)
        {}

        virtual R Invoke(void* instance, const Args&... args)
        {
            (void)instance;
            return m_function(args...);
        }

        virtual AZ::Uuid GetInstanceType() const
        {
            return AZ::Uuid::CreateNull();
        }

        virtual bool IsInvokable() const
        {
            return true;
        }

        virtual bool CanDomInvoke([[maybe_unused]] const AZ::Dom::Value& arguments) const
        {
            return CanInvokeFromDomArray<Args...>(arguments);
        }

        virtual AZ::Dom::Value DomInvoke(void* instance, const AZ::Dom::Value& arguments)
        {
            return InvokeFromDomArray<R, Args...>(AZStd::function<R(Args...)>([&](Args... args) -> R
            {
                return Invoke(instance, args...);
            }), arguments);
        }

        FunctionPtr m_function;
    };

    // Wraps a type that implements the C++ callable concept[i.e has either an overloaded operator() or a function pointer,
    // pointer to member data or pointer to member function]
    // If the type doesn't implement the callable concept stores the raw value type
    template<typename Invocable>
    class AttributeInvocable
        : public Attribute
    {
        using Callable = AZStd::conditional_t<AZStd::function_traits<Invocable>::value,
            AZStd::function<typename AZStd::function_traits<Invocable>::function_type>,
            AZStd::remove_cvref_t<Invocable>>;
    public:
        AZ_RTTI((AttributeInvocable<Invocable>, "{60D5804F-9AF4-4EB1-8F5A-62AFB4883F9D}", Invocable), AZ::Attribute);
        AZ_CLASS_ALLOCATOR(AttributeInvocable<Invocable>, SystemAllocator, 0);
        template<typename CallableType>
        explicit AttributeInvocable(CallableType&& invocable)
            : m_callable(AZStd::forward<CallableType>(invocable))
        {
        }

        AttributeUniquePtr GetVoidInstanceAttributeInvocable() override
        {
            // Make a temporary AttributeInvocable attribute that can cast from a void pointer
            // to the class type needed by this AttributeInvocable

            if constexpr (AZStd::function_traits<Callable>::value)
            {
                using CallableReturnType = typename AZStd::function_traits<Callable>::return_type;
                using VoidInstanceCallWrapper = typename AZStd::function_traits<Callable>::template expand_args<
                    Internal::FunctionObjectInvoker<CallableReturnType>::template ClassToVoidArgsInvoker>;
                return AttributeUniquePtr(new AttributeInvocable<typename AZStd::function_traits<VoidInstanceCallWrapper>::function_type>(
                    VoidInstanceCallWrapper{ m_callable }));
            }
            else
            {
                // Otherwise if the type being wrapped is not callable then assume it is a regular value type and
                // invoke base class version of this function
                return Attribute::GetVoidInstanceAttributeInvocable();
            }
        }

        template<typename... FuncArgs>
        decltype(auto) operator()(FuncArgs&&... args)
        {
            if constexpr (AZStd::function_traits<Invocable>::value)
            {
                static_assert(AZStd::is_invocable_v<Callable, FuncArgs...>, "Attribute stores Invocable type, but cannot be invoked with supplied arguments");
                return AZStd::invoke(m_callable, AZStd::forward<FuncArgs>(args)...);
            }
            else
            {
                return m_callable;
            }
        }

        virtual bool IsInvokable() const
        {
            return AZStd::function_traits<Invocable>::value;
        }

        virtual bool CanDomInvoke(const AZ::Dom::Value& arguments) const
        {
            return DomInvokeHelper<Callable>::CanInvoke(arguments);
        }

        virtual AZ::Dom::Value DomInvoke(void*, const AZ::Dom::Value& arguments)
        {
            return DomInvokeHelper<Callable>::Invoke(m_callable, arguments);
        }
    private:
        Callable m_callable;
    };

    // Deduction guide for AttributeInvocable type
    // If the type has valid function traits(i.e it fits the callable concept, then the function_type is retrieved from the function type),
    // otherwise the type is used without modification
    template<typename Invocable>
    AttributeInvocable(Invocable&&) -> AttributeInvocable<AZStd::conditional_t<AZStd::function_traits<Invocable>::value,
        typename AZStd::function_traits<Invocable>::function_type,
        AZStd::remove_cvref_t<Invocable>>>;

    /**
    * Generic attribute member function pointer container. All function must return non void result (we can implement this)
    * as this is attribute and assumed to return something. The instance to the class will provided vie the invoke function.
    * In general (unless you care) you should use this class vie the AttributeFunction class.
    */
    template<class T>
    class AttributeMemberFunction;

    template<class R, class C, class... Args>
    class AttributeMemberFunction<R(C::*)(Args...)>
        : public AttributeFunction<R(Args...)>
    {
    public:
        AZ_RTTI((AZ::AttributeMemberFunction<R(C::*)(Args...)>, "{F41F655D-87F7-4A87-9412-9AF4B528B142}", R, C, Args...), AttributeFunction<R(Args...)>);
        AZ_CLASS_ALLOCATOR(AttributeMemberFunction<R(C::*)(Args...)>, SystemAllocator, 0);
        typedef R(C::* FunctionPtr)(Args...);

        explicit AttributeMemberFunction(FunctionPtr f)
                : AttributeFunction<R(Args...)>(nullptr)
                , m_memFunction(f)
        {}

        R Invoke(void* instance, const Args&... args) override
        {
            return (reinterpret_cast<C*>(instance)->*m_memFunction)(args...);
        }

        AZ::Uuid GetInstanceType() const override
        {
            return AzTypeInfo<C>::Uuid();
        }

        FunctionPtr GetMemberFunctionPtr() const
        {
            return m_memFunction;
        }

        virtual bool IsInvokable() const
        {
            return true;
        }

        virtual bool CanDomInvoke([[maybe_unused]] const AZ::Dom::Value& arguments) const
        {
            return CanInvokeFromDomArray<Args...>(arguments);
        }

        virtual AZ::Dom::Value DomInvoke(void* instance, const AZ::Dom::Value& arguments)
        {
            return InvokeFromDomArray<R, Args...>(AZStd::function<R(Args...)>([&](Args... args) -> R
            {
                return Invoke(instance, args...);
            }), arguments);
        }
    private:
        FunctionPtr m_memFunction;
    };

    // const versions
    template<class R, class C, class... Args>
    class AttributeMemberFunction<R(C::*)(Args...) const>
        : public AttributeFunction<R(Args...)>
    {
    public:
        AZ_RTTI((AZ::AttributeMemberFunction<R(C::*)(Args...) const>, "{4E21155A-0FB0-4F11-999A-B946B5954A0A}", R, C, Args...), AttributeFunction<R(Args...)>);
        AZ_CLASS_ALLOCATOR(AttributeMemberFunction<R(C::*)(Args...) const>, SystemAllocator, 0);
        typedef R(C::* FunctionPtr)(Args...) const;

        explicit AttributeMemberFunction(FunctionPtr f)
            : AttributeFunction<R(Args...)>(nullptr)
            , m_memFunction(f)
        {}

        R Invoke(void* instance, const Args&... args) override
        {
            return (reinterpret_cast<const C*>(instance)->*m_memFunction)(args...);
        }

        AZ::Uuid GetInstanceType() const override
        {
            return AzTypeInfo<C>::Uuid();
        }

        FunctionPtr GetMemberFunctionPtr() const
        {
            return m_memFunction;
        }
    private:
        FunctionPtr m_memFunction;
    };

    template <typename T>
    using AttributeContainerType = AZStd::conditional_t<AZStd::is_member_pointer<T>::value,
        AZStd::conditional_t<AZStd::is_member_function_pointer<T>::value, AttributeMemberFunction<T>, AttributeMemberData<T> >,
        AZStd::conditional_t<AZStd::is_function<AZStd::remove_pointer_t<T>>::value, AttributeFunction<AZStd::remove_pointer_t<T>>,
        AZStd::conditional_t<AZStd::function_traits<T>::value, AttributeInvocable<typename AZStd::function_traits<T>::function_type>, AttributeData<T>>>
    >;

    namespace Internal
    {
        template<class T>
        struct AttributeValueTypeClassChecker
        {
            // By default any value is OK
            static bool Check(const Uuid&, IRttiHelper*)
            {
                return true;
            }
        };

        template<class T, class C>
        struct AttributeValueTypeClassChecker<T C::*>
        {
            static bool Check(const Uuid& classId, IRttiHelper* rtti)
            {
                if (classId == AzTypeInfo<C>::Uuid())
                {
                    return true;
                }
                return rtti ? rtti->IsTypeOf(AzTypeInfo<C>::Uuid()) : false;
            }
        };

        template<class T, class C>
        struct AttributeValueTypeClassChecker<T(C::*)()>
        {
            static bool Check(const Uuid& classId, IRttiHelper* rtti)
            {
                if (classId == AzTypeInfo<C>::Uuid())
                {
                    return true;
                }
                return rtti ? rtti->IsTypeOf(AzTypeInfo<C>::Uuid()) : false;
            }
        };

        template<class T, class C>
        struct AttributeValueTypeClassChecker<T(C::*)() const>
        {
            static bool Check(const Uuid& classId, IRttiHelper* rtti)
            {
                if (classId == AzTypeInfo<C>::Uuid())
                {
                    return true;
                }
                return rtti ? rtti->IsTypeOf(AzTypeInfo<C>::Uuid()) : false;
            }
        };

        template<class T, bool IsNoSpecialization = HasNoOnDemandReflection<OnDemandReflection<T>>::value>
        struct OnDemandReflectHook
        {
            static StaticReflectionFunctionPtr Get()
            {
                return nullptr;
            }
        };

        template<class T>
        struct OnDemandReflectHook<T, false>
        {
            static StaticReflectionFunctionPtr Get()
            {
                return &OnDemandReflection<T>::Reflect;
            }
        };
    }
}
