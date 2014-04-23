#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    //initialisation
#ifdef _WIN32
    jsonconfigFile = QCoreApplication::applicationDirPath() + "/gui-config.json";
#else
    QDir ssConfigDir = QDir::homePath() + "/.config/shadowsocks";
    jsonconfigFile = ssConfigDir.absolutePath() + "/gui-config.json";
    if (!ssConfigDir.exists()) {
        ssConfigDir.mkpath(ssConfigDir.absolutePath());
    }
#endif
    m_profile = new Profiles(jsonconfigFile);

    ui->profileComboBox->insertItems(0, m_profile->getProfileList());
    ui->stopButton->setEnabled(false);
    ui->laddrEdit->setValidator(&ipv4addrValidator);
    ui->sportEdit->setValidator(&portValidator);
    ui->lportEdit->setValidator(&portValidator);

    ui->debugCheck->setChecked(m_profile->isDebug());
    ui->autohideCheck->setChecked(m_profile->isAutoHide());
    ui->autostartCheck->setChecked(m_profile->isAutoStart());

    //desktop systray
    systrayMenu.addAction("Show/Hide", this, SLOT(showorhideWindow()));
    systrayMenu.addAction("Start", this, SLOT(startButtonPressed()));
    systrayMenu.addAction("Stop", this, SLOT(stopButtonPressed()));
    systrayMenu.addAction("Exit", this, SLOT(close()));
    systrayMenu.actions().at(2)->setEnabled(false);
#ifdef _WIN32
    systray.setIcon(QIcon(":/icon/black_icon.png"));
#else
    systray.setIcon(QIcon(":/icon/mono_icon.png"));
#endif
    systray.setToolTip(QString("Shadowsocks-Qt5"));
    systray.setContextMenu(&systrayMenu);
    systray.show();

    //Move to the center of the screen
    this->move(QApplication::desktop()->screen()->rect().center() - this->rect().center());

    //SIGNALs and SLOTs
    connect(&ss_local, &SS_Process::readReadyProcess, this, &MainWindow::onReadReadyProcess);
    connect(&ss_local, &SS_Process::sigstart, this, &MainWindow::processStarted);
    connect(&ss_local, &SS_Process::sigstop, this, &MainWindow::processStopped);
    connect(&systray, &QSystemTrayIcon::activated, this, &MainWindow::systrayActivated);

    connect(ui->backendToolButton, &QToolButton::clicked, this, &MainWindow::onBackendToolButtonPressed);
    connect(ui->profileComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &MainWindow::onCurrentProfileChanged);
    connect(ui->backendTypeCombo, &QComboBox::currentTextChanged, this, &MainWindow::backendTypeChanged);
    connect(ui->addProfileButton, &QToolButton::clicked, this, &MainWindow::addProfileDialogue);
    connect(&addProfileDlg, &AddProfileDialogue::inputAccepted, this, &MainWindow::onAddProfileDialogueAccepted);
    connect(&addProfileDlg, &AddProfileDialogue::inputRejected, this, &MainWindow::onAddProfileDialogueRejected);
    connect(ui->startButton, &QPushButton::clicked, this, &MainWindow::startButtonPressed);
    connect(ui->stopButton, &QPushButton::clicked, this, &MainWindow::stopButtonPressed);
    connect(ui->delProfileButton, &QToolButton::clicked, this, &MainWindow::deleteProfile);

    //update current configuration
    ui->profileComboBox->setCurrentIndex(m_profile->getIndex());
    ui->backendTypeCombo->setCurrentText(m_profile->getBackendType());
    /*
     * If there is only one config in gui-config.json, then the function above wouldn't emit signal.
     * Therefore, we have to emit a signal manually.
     */
    emit ui->profileComboBox->currentIndexChanged(m_profile->getIndex());
    emit ui->backendTypeCombo->currentTextChanged(m_profile->getBackendType());

    //connect signals and slots when config changed
    //Profile
    connect(this, &MainWindow::configurationChanged, this, &MainWindow::onConfigurationChanged);
    connect(ui->serverEdit, &QLineEdit::textChanged, this, &MainWindow::serverEditFinished);
    connect(ui->sportEdit, &QLineEdit::textChanged, this, &MainWindow::sportEditFinished);
    connect(ui->pwdEdit, &QLineEdit::textChanged, this, &MainWindow::pwdEditFinished);
    connect(ui->laddrEdit, &QLineEdit::textChanged, this, &MainWindow::laddrEditFinished);
    connect(ui->lportEdit, &QLineEdit::textChanged, this, &MainWindow::lportEditFinished);
    connect(ui->methodComboBox, &QComboBox::currentTextChanged, this, &MainWindow::methodChanged);
    connect(ui->timeoutSpinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &MainWindow::timeoutChanged);
    connect(ui->profileEditButtonBox, &QDialogButtonBox::clicked, this, &MainWindow::profileEditButtonClicked);

    //Misc
    connect(this, &MainWindow::miscConfigurationChanged, this, &MainWindow::onMiscConfigurationChanged);
    connect(ui->autohideCheck, &QCheckBox::stateChanged, this, &MainWindow::autoHideChecked);
    connect(ui->autostartCheck, &QCheckBox::stateChanged, this, &MainWindow::autoStartChecked);
    connect(ui->debugCheck, &QCheckBox::stateChanged, this, &MainWindow::debugChecked);
    connect(ui->miscSaveButton, &QPushButton::clicked, this, &MainWindow::miscButtonBoxClicked);
    connect(ui->aboutButton, &QPushButton::clicked, this, &MainWindow::aboutButtonClicked);
}

MainWindow::~MainWindow()
{
    ss_local.stop();//prevent crashes
    delete ui;
    delete m_profile;
}

const QString MainWindow::aboutText = QString("<h3>Platform-Cross GUI Client for Shadowsocks</h3><p>Version: 0.3.1</p><p>Copyright © 2014 William Wong (<a href='https://twitter.com/librehat'>@librehat</a>)</p><p>Licensed under LGPLv3<br />Project Hosted at <a href='https://github.com/librehat/shadowsocks-qt5'>GitHub</a></p>");

void MainWindow::onBackendToolButtonPressed()
{
    QString backend = QFileDialog::getOpenFileName();
    if (!backend.isEmpty()) {
        m_profile->setBackend(backend);
        ui->backendEdit->setText(m_profile->getBackend());
        emit configurationChanged();
    }
    this->setWindowState(Qt::WindowActive);
    this->activateWindow();
    ui->backendEdit->setFocus();
}

void MainWindow::backendTypeChanged(const QString &type)
{
    m_profile->setBackendType(type);

    //detect backend again no matter empty or not
    ui->backendEdit->setText(detectSSLocal());

    emit configurationChanged();
}

void MainWindow::onCurrentProfileChanged(int i)
{
    if (i < 0) {//there is no profile
        addProfileDialogue(true);//enforce
        return;
    }

    ss_local.stop();//Q: should we stop the backend when profile changed?
    m_profile->setIndex(i);
    current_profile = m_profile->getProfile(i);

    ui->serverEdit->setText(current_profile.server);
    ui->sportEdit->setText(current_profile.server_port);
    ui->pwdEdit->setText(current_profile.password);
    ui->laddrEdit->setText(current_profile.local_addr);
    ui->lportEdit->setText(current_profile.local_port);
    ui->methodComboBox->setCurrentText(current_profile.method);
    ui->timeoutSpinBox->setValue(current_profile.timeout.toInt());

    emit configurationChanged();
}

void MainWindow::addProfileDialogue(bool enforce = false)
{
    addProfileDlg.setEnforceMode(enforce);
    addProfileDlg.clear();
    addProfileDlg.show();
    addProfileDlg.exec();
}

void MainWindow::onAddProfileDialogueAccepted(const QString &name, bool u, const QString &uri)
{
    if(u) {
        m_profile->addProfileFromSSURI(name, uri);
    }
    else {
        m_profile->addProfile(name);
    }
    current_profile = m_profile->lastProfile();
    ui->profileComboBox->insertItem(ui->profileComboBox->count(), current_profile.profileName);

    //change serverComboBox, let it emit currentIndexChanged signal.
    ui->profileComboBox->setCurrentIndex(ui->profileComboBox->count() - 1);
}

void MainWindow::onAddProfileDialogueRejected(bool enforce)
{
    if (enforce) {
        m_profile->addProfile("");
        current_profile = m_profile->lastProfile();
        ui->profileComboBox->insertItem(ui->profileComboBox->count(), "");
        //since there was no item previously, serverComboBox would change itself automatically.
        //we don't need to emit the signal again.
    }
}

QString MainWindow::detectSSLocal()
{
    //Check if backendType matches current one
    if (Profiles::detectBackendTypeID(m_profile->getBackend()) == m_profile->getBackendTypeID()) {
        return m_profile->getBackend();
    }

    QString execName, sslocal;
    switch (m_profile->getBackendTypeID()) {
    case 1://nodejs
        execName = "sslocal";
        break;
    case 2://go
        execName = "shadowsocks-local";
        break;
    case 3:
#ifdef _WIN32
        execName = "python";//detect python to avoid the conflict with sslocal.cmd of nodejs
#else
        execName = "sslocal";
#endif
        break;
    default://including 0. libev
        execName = "ss-local";
    }

#ifdef _WIN32
    QStringList findPathsList(QCoreApplication::applicationDirPath());
#else
    QStringList findPathsList(QDir::homePath() + "/.config/shadowsocks/bin");
#endif
    sslocal = QStandardPaths::findExecutable(execName, findPathsList);//search ss-qt5 directory first
    if(sslocal.isEmpty()) {//if not found then search system's PATH
        sslocal = QStandardPaths::findExecutable(execName);
    }

    if(!sslocal.isEmpty()) {
        m_profile->setBackend(sslocal);
    }

    return m_profile->getBackend();
}

void MainWindow::saveProfile()
{
    m_profile->saveProfile(ui->profileComboBox->currentIndex(), current_profile);
    m_profile->saveProfileToJSON();
    emit onConfigurationChanged(true);
    emit miscConfigurationChanged(true);
}

void MainWindow::saveMiscConfig()
{
    m_profile->saveProfileToJSON();
    emit miscConfigurationChanged(true);
    emit onConfigurationChanged(true);
}

void MainWindow::profileEditButtonClicked(QAbstractButton *b)
{
    if (ui->profileEditButtonBox->standardButton(b) == QDialogButtonBox::Save) {
        saveProfile();
    }
    else {//reset
        m_profile->revert();
        ui->backendTypeCombo->setCurrentIndex(m_profile->getBackendTypeID());
        disconnect(ui->profileComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &MainWindow::onCurrentProfileChanged);
        ui->profileComboBox->clear();
        ui->profileComboBox->insertItems(0, m_profile->getProfileList());
        ui->profileComboBox->setCurrentIndex(m_profile->getIndex());
        connect(ui->profileComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &MainWindow::onCurrentProfileChanged);
        emit ui->profileComboBox->currentIndexChanged(m_profile->getIndex());//same in MainWindow's constructor
        emit onConfigurationChanged(true);
    }
}

void MainWindow::startButtonPressed()
{
    checkIfSaved();

    if (!m_profile->isValidate(current_profile)) {
        QMessageBox::critical(this, "Error", "Invalid profile or configuration.");
        return;
    }

    ss_local.setapp(m_profile->getBackend());
    ss_local.setTypeID(m_profile->getBackendTypeID());
    ss_local.start(current_profile);
}

void MainWindow::stopButtonPressed()
{
    ss_local.stop();
}

void MainWindow::deleteProfile()
{
    int i = ui->profileComboBox->currentIndex();
    m_profile->deleteProfile(i);
    ui->profileComboBox->removeItem(i);
}

void MainWindow::processStarted()
{
    systrayMenu.actions().at(1)->setEnabled(false);
    systrayMenu.actions().at(2)->setEnabled(true);
    ui->stopButton->setEnabled(true);
    ui->startButton->setEnabled(false);
    ui->logBrowser->clear();

    systray.setIcon(QIcon(":/icon/running_icon.png"));
}

void MainWindow::processStopped()
{
    systrayMenu.actions().at(1)->setEnabled(true);
    systrayMenu.actions().at(2)->setEnabled(false);
    ui->stopButton->setEnabled(false);
    ui->startButton->setEnabled(true);

#ifdef _WIN32
    systray.setIcon(QIcon(":/icon/black_icon.png"));
#else
    systray.setIcon(QIcon(":/icon/mono_icon.png"));
#endif
}

void MainWindow::showorhideWindow()
{
    if (this->isVisible()) {
        this->hide();
    }
    else {
        this->show();
        this->setWindowState(Qt::WindowActive);
        this->activateWindow();
        ui->startButton->setFocus();
    }
}

void MainWindow::systrayActivated(QSystemTrayIcon::ActivationReason r)
{
    if (r != 1) {
        showorhideWindow();
    }
}

void MainWindow::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::WindowStateChange && this->isMinimized()) {
        this->hide();
    }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    checkIfSaved();
    QWidget::closeEvent(e);
}

void MainWindow::onReadReadyProcess(const QByteArray &o)
{
    ui->logBrowser->moveCursor(QTextCursor::End);
    ui->logBrowser->append(o);
    ui->logBrowser->moveCursor(QTextCursor::End);
}

void MainWindow::onConfigurationChanged(bool saved)
{
    ui->profileEditButtonBox->setEnabled(!saved);
}

void MainWindow::onMiscConfigurationChanged(bool saved)
{
    ui->miscSaveButton->setEnabled(!saved);
}

void MainWindow::serverEditFinished(const QString &str)
{
    current_profile.server = str;
    emit configurationChanged();
}

void MainWindow::sportEditFinished(const QString &str)
{
    current_profile.server_port = str;
    emit configurationChanged();
}

void MainWindow::pwdEditFinished(const QString &str)
{
    current_profile.password = str;
    emit configurationChanged();
}

void MainWindow::laddrEditFinished(const QString &str)
{
    current_profile.local_addr = str;
    emit configurationChanged();
}

void MainWindow::lportEditFinished(const QString &str)
{
    current_profile.local_port = str;
    emit configurationChanged();
}

void MainWindow::methodChanged(const QString &m)
{
    current_profile.method = m;
    emit configurationChanged();
}

void MainWindow::timeoutChanged(int t)
{
    current_profile.timeout = QString::number(t);
    emit configurationChanged();
}

void MainWindow::autoHideChecked(int c)
{
    if (c == Qt::Checked) {
        m_profile->setAutoHide(true);
    }
    else
        m_profile->setAutoHide(false);

    emit miscConfigurationChanged();
}

void MainWindow::autoStartChecked(int c)
{
    if (c == Qt::Checked) {
        m_profile->setAutoStart(true);
    }
    else
        m_profile->setAutoStart(false);

    emit miscConfigurationChanged();
}

void MainWindow::debugChecked(int c)
{
    if (c == Qt::Checked) {
        m_profile->setDebug(true);
    }
    else
        m_profile->setDebug(false);

    emit miscConfigurationChanged();
}

void MainWindow::miscButtonBoxClicked()
{
    saveMiscConfig();
}

void MainWindow::checkIfSaved()
{
    if (ui->profileEditButtonBox->isEnabled()) {
        QMessageBox::StandardButton save = QMessageBox::question(this, "Unsaved Profile", "Current profile is not saved yet.\nDo you want to save it now?", QMessageBox::Save|QMessageBox::No, QMessageBox::Save);
        if (save == QMessageBox::Save) {
            saveProfile();
        }
    }
}

void MainWindow::aboutButtonClicked()
{
    QMessageBox::about(this, "About Shadowsocks-Qt5", aboutText);
}
