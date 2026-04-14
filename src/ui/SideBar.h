#pragma once
#include <QWidget>
#include <QPushButton>
#include <QVBoxLayout>

class SideBar : public QWidget {
    Q_OBJECT
public:
    explicit SideBar(QWidget* parent = nullptr);

    QPushButton* infoBtn() const { return m_infoBtn; }
    QPushButton* settingsBtn() const { return m_settingsBtn; }

private:
    QPushButton* m_infoBtn;
    QPushButton* m_settingsBtn;
};
