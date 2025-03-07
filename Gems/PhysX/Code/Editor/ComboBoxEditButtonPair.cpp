/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Editor/ComboBoxEditButtonPair.h>

namespace PhysX
{
    namespace Editor
    {
        ComboBoxEditButtonPair::ComboBoxEditButtonPair(QWidget* parent)
            : QWidget(parent)
        {
            QHBoxLayout* layout = new QHBoxLayout(this);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(0);

            m_comboBox = new QComboBox();
            m_comboBox->installEventFilter(this);

            m_editButton = new QToolButton();
            m_editButton->setAutoRaise(true);
            m_editButton->setToolTip(QString("Edit"));
            m_editButton->setIcon(QIcon(":/stylesheet/img/UI20/open-in-internal-app.svg"));

            layout->addWidget(m_comboBox);
            layout->addWidget(m_editButton);

            connect(m_comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ComboBoxEditButtonPair::onChildComboBoxValueChange);
        }

        bool ComboBoxEditButtonPair::value() const 
        {
            return m_comboBox->currentIndex() == 1;
        }

        void ComboBoxEditButtonPair::setValue(bool value) 
        {
            m_comboBox->blockSignals(true);
            m_comboBox->setCurrentIndex(value ? 1 : 0);
            m_comboBox->blockSignals(false);
        }

        void ComboBoxEditButtonPair::onChildComboBoxValueChange(int value)
        {
            emit valueChanged(value != 0);
        }

        QComboBox* ComboBoxEditButtonPair::GetComboBox()
        { 
            return m_comboBox; 
        }

        QToolButton* ComboBoxEditButtonPair::GetEditButton()
        { 
            return m_editButton; 
        }
    } // namespace Editor
} // namespace PhysX

#include <Editor/moc_ComboBoxEditButtonPair.cpp>
