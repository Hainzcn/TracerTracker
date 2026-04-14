#include "SideBar.h"
#include "Styles.h"
#include <QIcon>

SideBar::SideBar(QWidget* parent) : QWidget(parent) {
    setObjectName("sideBar");
    setFixedWidth(48);
    setStyleSheet(Styles::SIDE_BAR_STYLE());

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 12, 4, 12);
    layout->setSpacing(16);

    m_infoBtn = new QPushButton(this);
    m_infoBtn->setIcon(QIcon(":/svg/icons/Menu.svg"));
    m_infoBtn->setIconSize(QSize(20, 20));
    m_infoBtn->setCheckable(true);
    m_infoBtn->setStyleSheet(Styles::SIDE_BAR_BTN_STYLE());
    m_infoBtn->setToolTip("切换信息叠加层");

    m_settingsBtn = new QPushButton(this);
    m_settingsBtn->setIcon(QIcon(":/svg/icons/Cog.svg"));
    m_settingsBtn->setIconSize(QSize(20, 20));
    m_settingsBtn->setStyleSheet(Styles::SIDE_BAR_BTN_STYLE());
    m_settingsBtn->setToolTip("设置");

    layout->addWidget(m_infoBtn);
    layout->addWidget(m_settingsBtn);
    layout->addStretch();
}
