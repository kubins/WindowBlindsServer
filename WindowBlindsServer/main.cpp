/*!
  \file   main.cpp
  \author Standa Kubín
  \date   31.03.2016
  \brief  server aplikace pro řízení okenních rolet
*/

#include <QCoreApplication>
#include <QSharedPointer>
#include <QThread>
#include <QFile>
#include <QUdpSocket>
#include <QDebug>
#include <QSemaphore>

class GPIO_CONTROL : public QObject
{
    Q_OBJECT

public:
    enum class GPIO_Type
    {
        INPUT,  //!< vstup
        OUTPUT  //!< výstup
    };

    GPIO_CONTROL(QObject *pParent, quint16 nGPIO, GPIO_Type eType);
    virtual ~GPIO_CONTROL();

    //! vrátí hodnotu GPIO
    bool GetValue();
    //! nastaví hodnotu GPIO
    void SetValue(bool bValue);

private:
    QFile m_oFile;              //!< soubor pro zápis do nebo čtení z GPIO
    GPIO_Type m_eGPIO_Type;     //!< typ přístupu na GPIO
};

class BLIND_THREAD : public QThread
{
    Q_OBJECT

public:
    BLIND_THREAD(quint16 nGPIO_Pulse, quint16 nGPIO_Direction, quint16 nGPIO_FB, QSharedPointer<QSemaphore> Semaphore);
    virtual ~BLIND_THREAD();

    //! ukončí thread
    void Stop() { m_bStop = true; }
    //! vrací true, pokud thread běží
    bool IsRunning() const { return m_bRunning; }

    //! vrátí pozici rolety
    qint32 GetValuePercent() const { return (m_nTargetPosition * 100) / m_sc_nMaximumPositionValue; }
    //! nastaví pozici rolety
    void SetValuePercent(qint32 nValuePercent) { m_nTargetPosition = (m_sc_nMaximumPositionValue * nValuePercent) / 100; }

    //! spustí kalibraci
    void Calibre() { m_bCalibre = true; }

protected:
    //! vstupní bod nového threadu
    virtual void run();

private:
    enum class ACTION
    {
        None,           //!< neprobíhá žádná akce
        Calibration,    //!< probíhá kalibrace
        Movement        //!< probíhá standardní pohyb na nastavenou pozici
    };

    enum class DIRECTION
    {
        DOWN,   //!< roleta pojede nahoru
        UP      //!< roleta pojede dolu
    };

    static const quint32 m_sc_nMaximumPositionValue = 10240;    //!< počet impulsů krokového motoru pro sjetí rotety ze shora dolů

    QScopedPointer<GPIO_CONTROL> m_oPulse;      //!< GPIO pro řízení otáčení
    QScopedPointer<GPIO_CONTROL> m_oDirection;  //!< GPIO pro nastavení směru otáčení
    QScopedPointer<GPIO_CONTROL> m_oFB;         //!< GPIO pro získání stavu rulety

    QSharedPointer<QSemaphore> m_Semaphore;     //!< it controls access to the common HW resources

    bool m_bRunning;    //!< příznak běžícího threadu
    bool m_bStop;       //!< příznak pro ukončení threadu
    bool m_bCalibre;    //!< požadavek na spuštění kalibrace rolety

    quint32 m_nActualPosition;                  //!< aktuální pozice rolety
    quint32 m_nTargetPosition;                  //!< cílová pozice rolety
    quint32 m_nTargetPositionWorkingThread;     //!< cílová pozice rolety pro m_eAction == ACTION::Movement

    ACTION m_eAction;   //!< aktuální práce threadu

    //! nastaví směr pohybu
    void SetDirection(DIRECTION eDirection);
    //! vygeneruje impuls
    void GeneratePulse();
    //! vrací true, pokud je roleta úplně nahoře
    bool IsBlindUp();
};

class BLIND : public QObject
{
    Q_OBJECT

public:
    BLIND(QObject *pParent, quint16 nGPIO_Pulse, quint16 nGPIO_Direction, quint16 nGPIO_FB, QSharedPointer<QSemaphore> Semaphore);
    virtual ~BLIND() {}

    //! vrátí pozici rolety
    qint32 GetValuePercent() const;
    //! nastaví pozici rolety
    void SetValuePercent(qint32 nValuePercent);

private:
    QScopedPointer<BLIND_THREAD> m_oWorkingThread;  //!< thread řídící motor
    QSharedPointer<QSemaphore> m_Semaphore;         //!< it controls access to the common HW resources
};

class BLINDS_CONTROLLER : public QObject
{
    Q_OBJECT

public:
    BLINDS_CONTROLLER(QObject *pParent = Q_NULLPTR);
    virtual ~BLINDS_CONTROLLER() {}

private:
    struct Client
    {
        Client() : m_nPort(-1) {}
        Client(QHostAddress Address, qint32 nPort) : m_Address(Address), m_nPort(nPort) {}

        QHostAddress m_Address;     //!< client address
        qint32 m_nPort;             //!< client port

        inline bool operator==(const Client& X){ return X.m_Address == m_Address && X.m_nPort == m_nPort; }
        inline bool operator!=(const Client& X){ return X.m_Address != m_Address || X.m_nPort != m_nPort; }
    };

    QHash<qint32, QSharedPointer<BLIND> > m_arrBlinds;  //!< pole tříd pro řízení rolet
    QUdpSocket m_oUDP_SocketForReceiving;               //!< UDP socket bound to the specific port intended for window blind settings receiving
    QUdpSocket m_oUDP_SocketForSending;                 //!< UDP socket intended for clients information about the new states
    QSharedPointer<QSemaphore> m_Semaphore;             //!< it controls access to the common HW resources (it limites the number of simultaneously driven window blinds)
    QList<Client> m_arrClients;                         //!< list of registered clients (these clients are informed about the changes)

private slots:
    // reakce na příjem UDP paketu
    void OnUDP_ProcessPendingMessage();
};

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    QScopedPointer<BLINDS_CONTROLLER> oBLINDS_CONTROLLER(new BLINDS_CONTROLLER());

    return a.exec();
}

GPIO_CONTROL::GPIO_CONTROL(QObject *pParent, quint16 nGPIO, GPIO_Type eType) : QObject(pParent), m_eGPIO_Type(eType)
{
    // inicializujeme IO
    QFile oFile("/sys/class/gpio/export");
    if(oFile.open(QIODevice::WriteOnly))
    {
        QString strGPIO = QString::number(nGPIO);
        QString strDirection = "in";
        QIODevice::OpenModeFlag eOpenModeFlag = QIODevice::ReadOnly;
        if(eType == GPIO_Type::OUTPUT)
        {
            strDirection = "out";
            eOpenModeFlag = QIODevice::WriteOnly;
        }
        oFile.write(strGPIO.toLatin1());
        oFile.flush();
        oFile.close();
        oFile.setFileName("/sys/class/gpio/gpio" + strGPIO + "/direction");
        forever
        {
            if(oFile.open(QIODevice::WriteOnly))
            {
                oFile.write(QString(strDirection).toLatin1());
                oFile.flush();
                oFile.close();
                // otevřeme soubor pro nastavování nebo čtení hodnot
                m_oFile.setFileName("/sys/class/gpio/gpio" + strGPIO + "/value");
                if(!m_oFile.open(eOpenModeFlag))
                {
                    qDebug() << "cannot open file" << m_oFile.fileName();
                }
                break;
            }
            else
            {
                qDebug() << "cannot open file" << oFile.fileName();
            }
        }
    }
    else
    {
        qDebug() << "cannot open file" << oFile.fileName();
    }
}

GPIO_CONTROL::~GPIO_CONTROL()
{
    m_oFile.close();
}

bool GPIO_CONTROL::GetValue()
{
    if(m_oFile.isOpen())
    {
        QByteArray arrData = m_oFile.read(1);
        if(arrData.count())
        {
            return arrData.at(0) == '1';
        }
    }
    return false;
}

void GPIO_CONTROL::SetValue(bool bValue)
{
    if(m_eGPIO_Type == GPIO_Type::OUTPUT)
    {
        if(m_oFile.isOpen())
        {
            m_oFile.write(QString(bValue ? "1" : "0").toLatin1());
            m_oFile.flush();
        }
    }
}

BLIND_THREAD::BLIND_THREAD(quint16 nGPIO_Pulse, quint16 nGPIO_Direction, quint16 nGPIO_FB, QSharedPointer<QSemaphore> Semaphore) : QThread()
{
    m_oPulse.reset(new GPIO_CONTROL(this, nGPIO_Pulse, GPIO_CONTROL::GPIO_Type::OUTPUT));
    m_oDirection.reset(new GPIO_CONTROL(this, nGPIO_Direction, GPIO_CONTROL::GPIO_Type::OUTPUT));
    m_oFB.reset(new GPIO_CONTROL(this, nGPIO_FB, GPIO_CONTROL::GPIO_Type::INPUT));

    m_Semaphore = Semaphore;

    m_bRunning = false;
    m_bStop = false;
    m_bCalibre = false;

    m_nActualPosition = 0;
    m_nTargetPosition = 0;
    m_nTargetPositionWorkingThread = 0;

    m_eAction = ACTION::None;

    // spustíme kalibraci
    Calibre();
}

BLIND_THREAD::~BLIND_THREAD()
{
    if(IsRunning())
    {
        Stop();
        for(qint32 nTimeoutCounter = 0; nTimeoutCounter < 1000 && IsRunning(); nTimeoutCounter++)
        {
            QThread::msleep(1);
        }
        Q_ASSERT(!IsRunning());
    }
}

void BLIND_THREAD::run()
{
    m_bRunning = true;
    while(!m_bStop)
    {
        switch(m_eAction)
        {
        case ACTION::Calibration:
            if(IsBlindUp())
            {
                // hotovo
                m_eAction = ACTION::None;
                m_nActualPosition = 0;
                m_bCalibre = false;
                // it releases HW resource
                m_Semaphore.data()->release();
            }
            else
            {
               GeneratePulse();
            }
            break;
        case ACTION::Movement:
            if(m_nActualPosition == m_nTargetPositionWorkingThread)
            {
                // hotovo
                m_eAction = ACTION::None;
                // it releases HW resource
                m_Semaphore.data()->release();
            }
            if(m_nActualPosition < m_nTargetPositionWorkingThread)
            {
                GeneratePulse();
                m_nActualPosition++;
            }
            if(m_nActualPosition > m_nTargetPositionWorkingThread)
            {
                GeneratePulse();
                m_nActualPosition--;
            }
            break;
        case ACTION::None:
            if(m_bCalibre)
            {
                // it tries to acquire HW resource
                if(m_Semaphore.isNull() || !m_Semaphore.data()->tryAcquire())
                {
                    break;
                }
                // kalibrace
                m_eAction = ACTION::Calibration;
                SetDirection(DIRECTION::UP);
                break;
            }
            if(m_nActualPosition != m_nTargetPosition)
            {
                // it tries to acquire HW resource
                if(m_Semaphore.isNull() || !m_Semaphore.data()->tryAcquire())
                {
                    break;
                }
                // pohyb
                m_eAction = ACTION::Movement;
                m_nTargetPositionWorkingThread = m_nTargetPosition;
                SetDirection(m_nActualPosition < m_nTargetPositionWorkingThread ? DIRECTION::DOWN : DIRECTION::UP);
                break;
            }
            QThread::msleep(1);
            break;
        }
    }
    m_bRunning = false;
}

void BLIND_THREAD::SetDirection(BLIND_THREAD::DIRECTION eDirection)
{
    if(!m_oDirection.isNull())
    {
        m_oDirection.data()->SetValue(eDirection == DIRECTION::DOWN ? 1 : 0);
    }
}

void BLIND_THREAD::GeneratePulse()
{
    // maximální rychlost = 1 KHz
    if(!m_oPulse.isNull())
    {
        m_oPulse.data()->SetValue(true);
        QThread::usleep(500);
        m_oPulse.data()->SetValue(false);
        QThread::usleep(500);
    }
}

bool BLIND_THREAD::IsBlindUp()
{
    if(!m_oFB.isNull())
    {
        return m_oFB.data()->GetValue();
    }
    return false;
}

BLIND::BLIND(QObject *pParent, quint16 nGPIO_Pulse, quint16 nGPIO_Direction, quint16 nGPIO_FB, QSharedPointer<QSemaphore> Semaphore) : QObject(pParent)
{
    m_oWorkingThread.reset(new BLIND_THREAD(nGPIO_Pulse, nGPIO_Direction, nGPIO_FB, Semaphore));
    // odstartujeme thread
    m_oWorkingThread.data()->start();
}

qint32 BLIND::GetValuePercent() const
{
    if(!m_oWorkingThread.isNull())
    {
        return m_oWorkingThread.data()->GetValuePercent();
    }
    return 0;
}

void BLIND::SetValuePercent(qint32 nValuePercent)
{
    if(!m_oWorkingThread.isNull())
    {
        m_oWorkingThread.data()->SetValuePercent(nValuePercent);
    }
}

BLINDS_CONTROLLER::BLINDS_CONTROLLER(QObject *pParent) : QObject(pParent)
{
    // it sets the maximum count of simultaneously driven window blinds
    m_Semaphore.reset(new QSemaphore(2));
    // nastavíme rolety
    m_arrBlinds[1] = QSharedPointer<BLIND>(new BLIND(this, 178, 193, 199, m_Semaphore));
    // nastavíme UDP soket pro příjem požaadvků
    connect(&m_oUDP_SocketForReceiving, SIGNAL(readyRead()), this, SLOT(OnUDP_ProcessPendingMessage()), Qt::UniqueConnection);
    if(!m_oUDP_SocketForReceiving.bind(5674))
    {
        qDebug() << "cannot bind UDP communication port";
    }
}

void BLINDS_CONTROLLER::OnUDP_ProcessPendingMessage()
{
    QByteArray arrDatagram;
    QHostAddress SenderAddress;
    quint16 nSenderPort = 0;

    while(m_oUDP_SocketForReceiving.hasPendingDatagrams())
    {
        arrDatagram.resize(m_oUDP_SocketForReceiving.pendingDatagramSize());
        if(m_oUDP_SocketForReceiving.readDatagram(arrDatagram.data(), arrDatagram.size(), &SenderAddress, &nSenderPort) == -1)
        {
            qDebug() << "unable to read UDP datagram";
        }
        else
        {
            QStringList arr_strMessages = QString(arrDatagram).split("#");
            foreach(QString strMessage, arr_strMessages)
            {
                QStringList arr_strMessageData = strMessage.split(";");
                if(arr_strMessageData.count() >= 1)
                {
                    if(arr_strMessageData.at(0) == "set_blind")
                    {
                        if(arr_strMessageData.count() >= 4)
                        {
                            qint32 nID = arr_strMessageData.at(1).toInt();
                            qint32 nPercentValue = arr_strMessageData.at(2).toInt();
                            if(m_arrBlinds.contains(nID))
                            {
                                if(nPercentValue > 100)
                                {
                                    nPercentValue = 100;
                                }
                                if(nPercentValue < 0)
                                {
                                    nPercentValue = 0;
                                }
                                qDebug() << "blind ID" << nID << "value" << nPercentValue;
                                if(!m_arrBlinds[nID].isNull())
                                {
                                    m_arrBlinds[nID].data()->SetValuePercent(nPercentValue);
                                    // send the new value to the other clients
                                    Client ClientHasSetTheValue(SenderAddress, arr_strMessageData.at(3).toInt());
                                    foreach(Client ClientToBeInformed, m_arrClients)
                                    {
                                        if(ClientToBeInformed != ClientHasSetTheValue)
                                        {
                                            if(!m_oUDP_SocketForSending.writeDatagram(QByteArray(QString("blind_position;" + arr_strMessageData.at(1) + ";" + arr_strMessageData.at(2)).toLatin1() + "#"), ClientToBeInformed.m_Address, ClientToBeInformed.m_nPort) == -1)
                                            {
                                                qDebug() << "error while sending the new window blind position";
                                            }
                                        }
                                    }
                                }
                            }
                            else
                            {
                                qDebug() << "unknown blind ID";
                            }
                        }
                        else
                        {
                            qDebug() << "unknown UDP data format";
                        }
                    }
                    if(arr_strMessageData.at(0) == "register")
                    {
                        if(arr_strMessageData.count() >= 2)
                        {
                            Client NewClientToBeRegistrated(SenderAddress, arr_strMessageData.at(1).toInt());
                            qDebug() << "new client registration attempt, client address" << NewClientToBeRegistrated.m_Address << "port" << NewClientToBeRegistrated.m_nPort;
                            if(!m_arrClients.contains(NewClientToBeRegistrated))
                            {
                                m_arrClients.append(NewClientToBeRegistrated);
                                // refresh the client by the actual window blind positions
                                QHashIterator<qint32, QSharedPointer<BLIND> > iBlinds(m_arrBlinds);
                                while(iBlinds.hasNext())
                                {
                                    iBlinds.next();
                                    if(!m_oUDP_SocketForSending.writeDatagram(QByteArray(QString("blind_position;" + QString::number(iBlinds.key()) + ";" + QString::number(iBlinds.value().data()->GetValuePercent()) + "#").toLatin1()), NewClientToBeRegistrated.m_Address, NewClientToBeRegistrated.m_nPort) == -1)
                                    {
                                        qDebug() << "error while sending the new window blind position";
                                    }
                                }
                            }
                        }
                        else
                        {
                            qDebug() << "unknown UDP data format";
                        }
                    }
                }
                else
                {
                    qDebug() << "unknown UDP data format";
                }
            }
        }
    }
}

#include "main.moc"
