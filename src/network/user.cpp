/****************************************************************************
**
** Copyright (C) TERIFLIX Entertainment Spaces Pvt. Ltd. Bengaluru
** Author: Prashanth N Udupa (prashanth.udupa@teriflix.com)
**
** This code is distributed under GPL v3. Complete text of the license
** can be found here: https://www.gnu.org/licenses/gpl-3.0.txt
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
****************************************************************************/

#include "user.h"
#include "application.h"
#include "timeprofiler.h"
#include "scritedocument.h"
#include "jsonhttprequest.h"

#include <QtDebug>
#include <QPainter>
#include <QDateTime>
#include <QJsonDocument>
#include <QCoreApplication>

static QString GetSessionExpiredErrorMessage()
{
    return QStringLiteral("Your login session has expired and therefore your device is deactivated. Please connect to the Internet and login/reactivate your installation of Scrite.");
}

struct CityCountryInfo
{
    CityCountryInfo();
    QStringList countryNames;
    QMap< QString,QList<int> > cityCountryName;
};
static const CityCountryInfo & GlobalCityCountryInfo()
{
    static CityCountryInfo theInstance;
    return theInstance;
}

CityCountryInfo::CityCountryInfo()
{
    QFile ccdb( QStringLiteral(":/misc/city-country-map.json.compressed") );
    if(!ccdb.open(QFile::ReadOnly))
        return;

    const QByteArray json = qUncompress(ccdb.readAll());
    const QJsonObject jsonMap = QJsonDocument::fromJson(json).object();
    QJsonObject::const_iterator it = jsonMap.constBegin();
    QJsonObject::const_iterator end = jsonMap.constEnd();
    QSet<QString> countries;
    while(it != end)
    {
        const QString country = it.value().toString();
        countries += country;
        ++it;
    }

    this->countryNames = countries.toList();

    it = jsonMap.constBegin();
    while(it != end)
    {
        const QString country = it.value().toString();
        const int cindex = this->countryNames.indexOf(country);
        cityCountryName[it.key()].append(cindex);
        ++it;
    }
}

User *User::instance()
{
    ::GlobalCityCountryInfo();

    static User *theUser = new User(qApp);
    return theUser;
}

User::User(QObject *parent)
    :QObject(parent)
{
    const QStringList appArgs = qApp->arguments();
    const QString starg = QStringLiteral("--sessionToken");
    const int stargPos = appArgs.indexOf(starg);
    if(stargPos >= 0 && appArgs.size() >= stargPos+2)
    {
        const QString stok = appArgs.at(stargPos+1);
        JsonHttpRequest::store( QStringLiteral("sessionToken"), stok );
    }

    QMetaObject::invokeMethod(this, "firstReload", Qt::QueuedConnection);

    connect(this, &User::infoChanged, this, &User::loggedInChanged);
    connect(this, &User::installationsChanged, this, &User::loggedInChanged);

    m_touchLogTimer.setSingleShot(false);
    // m_touchLogTimer.setInterval(5000);
    m_touchLogTimer.setInterval(10 * 60 * 1000); // 10 minutes
    connect(&m_touchLogTimer, &QTimer::timeout, this, [=]() {
        this->logActivity1(QStringLiteral("touch"));
    });
    connect(this, &User::loggedInChanged, this, [=]() {
        if(this->isLoggedIn())
            m_touchLogTimer.start();
        else
            m_touchLogTimer.stop();
    });
}

User::~User()
{

}

bool User::isLoggedIn() const
{
    return !m_info.isEmpty() && !m_installations.isEmpty();
}

QString User::email() const
{
    return m_info.value(QStringLiteral("email")).toString().toLower();
}

QString User::firstName() const
{
    return m_info.value(QStringLiteral("firstName")).toString();
}

QString User::lastName() const
{
    return m_info.value(QStringLiteral("lastName")).toString();
}

QString User::fullName() const
{
    return QStringList({this->firstName(), this->lastName()}).join(QStringLiteral(" ")).trimmed();
}

QString User::city() const
{
    return m_info.value(QStringLiteral("city")).toString();
}

QString User::country() const
{
    return m_info.value(QStringLiteral("country")).toString();
}

QString User::experience() const
{
    return m_info.value(QStringLiteral("experience")).toString();
}

QStringList User::countryNames()
{
    return ::GlobalCityCountryInfo().countryNames;
}

QStringList User::cityNames()
{
    return ::GlobalCityCountryInfo().cityCountryName.keys();
}

QStringList User::countries(const QString &cityName)
{
    const QList<int> idxList = ::GlobalCityCountryInfo().cityCountryName.value(cityName);
    if(idxList.isEmpty())
        return QStringList();

    const QStringList &countryNames = ::GlobalCityCountryInfo().countryNames;

    QStringList ret;
    for(int idx : idxList)
    {
        if(idx >= 0 || idx < countryNames.size())
            ret << countryNames.at(idx);
    }

    return ret;
}

bool User::isFeatureNameEnabled(const QString &featureName) const
{
    if(m_info.isEmpty())
        return false;

    const QString lfeatureName = featureName.toLower();
    const QJsonArray features = m_info.value( QStringLiteral("enabledAppFeatures") ).toArray();
    const auto featurePredicate = [lfeatureName](const QJsonValue &item) -> bool {
        const QString istring = item.toString().toLower();
        return (istring == lfeatureName);
    };
    const auto wildCardPredicate = [](const QJsonValue &item) -> bool { return item.toString() == QStringLiteral("*"); };
    const auto notFeaturePredicate = [lfeatureName](const QJsonValue &item) -> bool {
        const QString istring = item.toString().toLower();
        return istring.startsWith(QChar('!')) && (istring.mid(1) == lfeatureName);
    };

    const bool featureEnabled = std::find_if(features.constBegin(), features.constEnd(), featurePredicate) != features.constEnd();
    const bool allFeaturesEnabled = std::find_if(features.constBegin(), features.constEnd(), wildCardPredicate) != features.constEnd();
    const bool featureDisabled = std::find_if(features.constBegin(), features.constEnd(), notFeaturePredicate) != features.constEnd();
    return (allFeaturesEnabled || featureEnabled) && !featureDisabled;
}

void User::refresh()
{
    emit infoChanged();
    emit installationsChanged();
}

void User::setInfo(const QJsonObject &val)
{
    m_info = val;
    m_enabledFeatures.clear();
    m_analyticsConsent = false;

    if(!m_info.isEmpty())
    {
        static const QStringList availableFeatures = {
            QStringLiteral("screenplay"), QStringLiteral("structure"), QStringLiteral("notebook"),
            QStringLiteral("relationshipgraph"),
            QStringLiteral("scriptalay"), QStringLiteral("template"), QStringLiteral("report"),
            QStringLiteral("import"), QStringLiteral("export"), QStringLiteral("scrited")
        };
        const QJsonArray features = m_info.value( QStringLiteral("enabledAppFeatures") ).toArray();
        QSet<int> ifeatures;
        for(const QJsonValue &featureItem : features)
        {
            const QString feature = featureItem.toString().toLower();
            if(feature.isEmpty())
                continue;

            if(feature == QStringLiteral("*"))
            {
                for(int i=MinFeature; i<=MaxFeature; i++)
                    ifeatures += i;
            }
            else
            {
                const bool invert = feature.startsWith(QChar('!'));
                const int index = availableFeatures.indexOf(invert ? feature.mid(1) : feature);
                if(index >= 0)
                {
                    if(invert)
                        ifeatures -= index;
                    else
                        ifeatures += index;
                }
            }
        }

        m_enabledFeatures = ifeatures.toList();
        std::sort(m_enabledFeatures.begin(), m_enabledFeatures.end());

        const QJsonObject consentObj = m_info.value( QStringLiteral("consent") ).toObject();
        m_analyticsConsent = consentObj.value( QStringLiteral("activity") ).toBool(false);

#ifndef QT_NODEBUG
        qDebug() << "PA: " << m_enabledFeatures << m_info;
#endif
    }

    emit infoChanged();
}

void User::setInstallations(const QJsonArray &val)
{
    if(m_installations == val)
        return;

    m_installations = val;
    m_currentInstallationIndex = -1;

    auto sortVersionsArray = [](QJsonObject &object, const QString &key) {
        QJsonArray array = object.value(key).toArray();
        if(array.isEmpty())
            return;

        QVariantList varList = array.toVariantList();
        std::sort(varList.begin(), varList.end(), [](const QVariant &va, const QVariant &vb) {
            const QString vas = va.toString();
            const QString vbs = vb.toString();
            return QVersionNumber::fromString(vas) > QVersionNumber::fromString(vbs);
        });

        array = QJsonArray::fromVariantList(varList);
        object.insert(key, array);
    };

    // Sort version fields appVersions and pastVersions
    for(QJsonValueRef item : m_installations)
    {
        QJsonObject installation = item.toObject();
        sortVersionsArray(installation, QStringLiteral("appVersions"));
        sortVersionsArray(installation, QStringLiteral("pastVersions"));
        item = installation;
    }

    int index = -1;
    for(const QJsonValue &item : qAsConst(m_installations))
    {
        ++index;
        const QJsonObject installation = item.toObject();
        if(installation.value(QStringLiteral("deviceId")).toString() == JsonHttpRequest::deviceId())
        {
            const QString dtFormat = QStringLiteral("yyyy-MM-ddThh:mm:ss.zzz");
            const QString lastActivatedDateString = installation.value(QStringLiteral("lastActivationDate")).toString();
            if(!lastActivatedDateString.isEmpty())
            {
                const QDateTime lastActivateDate = QDateTime::fromString( lastActivatedDateString.left(lastActivatedDateString.length()-1), dtFormat );
                const int nrDaysFromLastActivation = lastActivateDate.daysTo(QDateTime::currentDateTime());
                if( nrDaysFromLastActivation > 28 )
                    break;
            }

            m_currentInstallationIndex = index;
            break;
        }
    }

    if(m_currentInstallationIndex < 0 && !m_installations.isEmpty())
    {
        m_errorReport->setErrorMessage(GetSessionExpiredErrorMessage());
        m_installations = QJsonArray();
        this->logout();
    }

    emit installationsChanged();
}

void User::firstReload()
{
    this->loadStoredUserInformation();
    this->reload();
}

void User::reset()
{
    ScriteDocument *document = ScriteDocument::instance();
    if(document)
        document->reset();

    this->setInstallations( QJsonArray() );
    this->setInfo( QJsonObject() );
    JsonHttpRequest::store( QStringLiteral("devices"), QVariant() );
    JsonHttpRequest::store( QStringLiteral("userInfo"), QVariant() );
    JsonHttpRequest::store( QStringLiteral("loginToken"), QVariant() );
    JsonHttpRequest::store( QStringLiteral("sessionToken"), QVariant() );
}

void User::activateCallDone()
{
    if(m_call)
    {
        if(m_call->hasError() || !m_call->hasResponse())
        {
            m_errorReport->setErrorMessage(m_call->errorText(), m_call->error());
            this->reset();
            emit forceLoginRequest();
            return;
        }

        const QJsonObject tokens = m_call->responseData();
        const QString sessionTokenKey = QStringLiteral("sessionToken");
        const QString sessionToken = tokens.value(sessionTokenKey).toString();
        m_call->store( sessionTokenKey, sessionToken );
    }

    // Get user information
    m_call = this->newCall();
    connect(m_call, &JsonHttpRequest::finished, this, &User::userInfoCallDone);
    m_call->setApi( QStringLiteral("user/me") );
    m_call->setType(JsonHttpRequest::GET);
    m_call->call();
}

void User::userInfoCallDone()
{
    if(m_call)
    {
        if(m_call->hasError() || !m_call->hasResponse())
        {
            m_errorReport->setErrorMessage(m_call->errorText(), m_call->error());
            this->reset();
            emit forceLoginRequest();
            return;
        }

        const QString ikey = QStringLiteral("installations");

        QJsonObject userInfo = m_call->responseData();
        QJsonValue installationsValue = userInfo.value(ikey);
        userInfo.remove(ikey);
        this->setInfo(userInfo);
        this->storeUserInfo();

        if(installationsValue.isArray())
        {
            const QJsonArray installations = installationsValue.toArray();
            this->setInstallations( installations );
            this->storeInstallations();
            return;
        }
    }

    // Fetch installations information
    m_call = this->newCall();
    connect(m_call, &JsonHttpRequest::finished, this, &User::installationsCallDone);
    m_call->setApi( QStringLiteral("user/installations") );
    m_call->setType(JsonHttpRequest::GET);
    m_call->call();
}

void User::installationsCallDone()
{
    if(m_call)
    {
        if(m_call->hasError() || !m_call->hasResponse())
        {
            m_errorReport->setErrorMessage(m_call->errorText(), m_call->error());
            this->reset();
            emit forceLoginRequest();
            return;
        }

        const QJsonObject installationsInfo = m_call->responseData();
        const QJsonArray installations = installationsInfo.value(QStringLiteral("list")).toArray();
        this->setInstallations( installations );
        this->storeInstallations();

        // All done.
    }
}

void User::loadStoredUserInformation()
{
    // Load information stored in the previous session
    const QVariant userInfoVariant = JsonHttpRequest::fetch( QStringLiteral("userInfo") );
    if(userInfoVariant.isValid() && !userInfoVariant.isNull() && userInfoVariant.canConvert(QMetaType::QString))
    {
        QJsonParseError parseError;
        const QString cryptText = userInfoVariant.toString();
        const QString crypt = JsonHttpRequest::decrypt(cryptText);
        const QJsonObject object = QJsonDocument::fromJson(crypt.toLatin1(), &parseError).object();
        if(parseError.error == QJsonParseError::NoError && !object.isEmpty())
            this->setInfo(object);
        else
        {
            m_errorReport->setErrorMessage(GetSessionExpiredErrorMessage());
            this->reset();
            return;
        }
    }
    else
        return;

    const QVariant devicesVariant = JsonHttpRequest::fetch( QStringLiteral("devices") );
    if(devicesVariant.isValid() && !devicesVariant.isNull() && devicesVariant.canConvert(QMetaType::QString))
    {
        QJsonParseError parseError;
        const QString cryptText = devicesVariant.toString();
        const QString crypt = JsonHttpRequest::decrypt(cryptText);
        const QJsonArray array = QJsonDocument::fromJson(crypt.toLatin1(), &parseError).array();
        if(parseError.error == QJsonParseError::NoError && !array.isEmpty())
            this->setInstallations(array);
        else
        {
            m_errorReport->setErrorMessage(GetSessionExpiredErrorMessage());
            this->reset();
        }
    }
}

JsonHttpRequest *User::newCall()
{
    if(m_call)
    {
        disconnect(m_call, &JsonHttpRequest::destroyed, this, &User::onCallDestroyed);
        m_call->deleteLater();
        m_call = nullptr;
    }

    m_call = new JsonHttpRequest(this);
    m_call->setAutoDelete(true);
    connect(m_call, &JsonHttpRequest::destroyed, this, &User::onCallDestroyed);
    connect(m_call, &JsonHttpRequest::justIssuedCall, m_errorReport, &ErrorReport::clear);
    emit busyChanged();
    return m_call;
}

void User::onCallDestroyed()
{
    m_call = nullptr;
    emit busyChanged();

#ifndef QT_NODEBUG
    qDebug() << "PA: ";
#endif
}

void User::onLogActivityCallFinished()
{
    JsonHttpRequest *call = qobject_cast<JsonHttpRequest*>(this->sender());
    if(call == nullptr)
        return;

    if(call->hasError())
    {
        const QStringList errorCodes({QStringLiteral("E_NO_ACTIVATION"),QStringLiteral("E_NO_SESSION"),QStringLiteral("E_NO_USER")});
        if(errorCodes.contains(call->errorCode()))
        {
            m_errorReport->setErrorMessage( ::GetSessionExpiredErrorMessage() );
            this->logout();
            return;
        }

        // Other error codes are fine, no issues
    }
}

void User::onDeactivateInstallationFinished()
{
    JsonHttpRequest *call = qobject_cast<JsonHttpRequest*>(this->sender());
    if(call == nullptr)
        return;

    if(call->hasError() || !call->hasResponse())
    {
        m_errorReport->setErrorMessage( QStringLiteral("Could not deactivate installation."), call->error() );
        return;
    }

    const QJsonObject response = call->responseData();
    const QJsonArray installations = response.value( QStringLiteral("list") ).toArray();
    this->setInstallations(installations);
    this->storeInstallations();
}

void User::storeUserInfo()
{
    const QString text = QJsonDocument(m_info).toJson();
    const QString cryptText = JsonHttpRequest::encrypt(text);
    JsonHttpRequest::store( QStringLiteral("userInfo"), cryptText );
}

void User::storeInstallations()
{
    const QString text = QJsonDocument(m_installations).toJson();
    const QString cryptText = JsonHttpRequest::encrypt(text);
    JsonHttpRequest::store( QStringLiteral("devices"), cryptText );
}

void User::reload()
{
    if(m_call != nullptr)
        return;

    // User should have logged in once.
    if(JsonHttpRequest::loginToken().isEmpty() || JsonHttpRequest::email().isEmpty())
    {
        this->setInfo(QJsonObject());
        this->setInstallations(QJsonArray());
        emit forceLoginRequest();
        return;
    }

    if( JsonHttpRequest::sessionToken().isEmpty() )
    {
        // Activate device to get session token
        m_call = this->newCall();
        connect(m_call, &JsonHttpRequest::finished, this, &User::activateCallDone);
        m_call->setApi( QStringLiteral("app/activate") );
        m_call->setData( QJsonObject({
                { QStringLiteral("email"), JsonHttpRequest::email() },
                { QStringLiteral("clientId"), JsonHttpRequest::clientId() },
                { QStringLiteral("deviceId"), JsonHttpRequest::deviceId() },
                { QStringLiteral("appVersion"), JsonHttpRequest::appVersion() },
                { QStringLiteral("loginToken"), JsonHttpRequest::loginToken() }
            }));
        m_call->call();
    }
    else
    {
        // Since we have session token, we can reload user information and
        // installation info.
        this->activateCallDone();
    }
}

void User::logout()
{
    ScriteDocument *document = ScriteDocument::instance();
    if(document && document->isModified())
    {
        m_errorReport->setErrorMessage( QStringLiteral("Current document is not saved. Please save the document before logging out.") );
        return;
    }

    JsonHttpRequest *call = new JsonHttpRequest(this);
    call->setAutoDelete(true);
    call->setType(JsonHttpRequest::POST);
    call->setApi(QStringLiteral("app/deactivate"));
    call->call(); // Fire and forget.

    this->reset();
}

void User::update(const QJsonObject &newInfo)
{
    if(JsonHttpRequest::sessionToken().isEmpty())
        return;

    JsonHttpRequest *call = this->newCall();
    call->setAutoDelete(true);
    call->setType(JsonHttpRequest::POST);
    call->setApi( QStringLiteral("user/me") );
    call->setData(newInfo);
    connect(call, &JsonHttpRequest::finished, this, [=]() {
        if(call->hasError())
            m_errorReport->setErrorMessage(call->errorText(), call->error());
        else if(call->hasResponse())
            this->setInfo(call->responseData());
        else {
            const QString errMsg = QStringLiteral("Couldn't update user information.");
            m_errorReport->setErrorMessage(errMsg,
                   QJsonObject({{QStringLiteral("code"), QStringLiteral("E_USERINFO")},
                                {QStringLiteral("text"), errMsg}}));
        }
    });
    call->call();
}

void User::deactivateInstallation(const QString &id)
{
    if(id.isEmpty() || !this->isLoggedIn())
        return;

    JsonHttpRequest *call = this->newCall();
    call->setType(JsonHttpRequest::POST);
    call->setApi(QStringLiteral("user/deactivateInstallation"));
    call->setData( QJsonObject({{QStringLiteral("installationId"),id}}) );
    connect(call, &JsonHttpRequest::finished, this, &User::onDeactivateInstallationFinished);
    call->call();
}

void User::refreshInstallations()
{
    if(m_call)
        return;

    m_call = this->newCall();
    m_call->setAutoDelete(true);
    m_call->setType(JsonHttpRequest::GET);
    m_call->setApi(QStringLiteral("user/installations"));
    connect(m_call, &JsonHttpRequest::finished, this, &User::installationsCallDone);
    m_call->call();
}

void User::logActivity2(const QString &givenActivity, const QJsonValue &data)
{
    if(JsonHttpRequest::sessionToken().isEmpty() || !m_analyticsConsent)
        return;

    const QString activity = givenActivity.isEmpty() ? QStringLiteral("touch") : givenActivity.toLower().simplified();

    // !!!NOT CALLING newCall() on PURPOSE!!!!
    // While logging activity, we do not need User.busy to become true
    JsonHttpRequest *call = new JsonHttpRequest(this);
    call->setAutoDelete(true);
    call->setType(JsonHttpRequest::POST);
    call->setApi( QStringLiteral("activity/log") );
    const QJsonObject callData = {
        { QStringLiteral("appVersion"), Application::instance()->applicationVersion() },
        { QStringLiteral("activity"), activity },
        { QStringLiteral("data"), data },
    };
    call->setData(callData);
    connect(call, &JsonHttpRequest::finished, this, &User::onLogActivityCallFinished);
    call->call(); // Fire and Forget

    // Trigger a touch log after 10 minutes
    m_touchLogTimer.stop();
    m_touchLogTimer.start();
}

///////////////////////////////////////////////////////////////////////////////

UserIconProvider::UserIconProvider() : QQuickImageProvider(QQmlImageProviderBase::Image) { }

UserIconProvider::~UserIconProvider() { }

QImage UserIconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(id);

    const int dim = qMax(100, qMin(requestedSize.width(),requestedSize.height()));

    QImage image(QSize(dim,dim), QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    QPainter paint(&image);

    QColor appPurple("#65318f");
    paint.setPen( QPen(appPurple,2.0) );

    appPurple.setAlphaF(0.9);
    paint.setBrush(appPurple);

    paint.setRenderHint(QPainter::Antialiasing);
    paint.drawEllipse(image.rect().adjusted(2, 2, -2, -2));

    if(User::instance()->isLoggedIn())
    {
        const QString email = JsonHttpRequest::email();
        const QString firstName = User::instance()->info().value(QStringLiteral("firstName")).toString();
        const QString lastName = User::instance()->info().value(QStringLiteral("lastName")).toString();

        QString initials;

        if(firstName.isEmpty() && lastName.isEmpty())
        {
            if(!email.isEmpty())
                initials = email.at(0).toUpper();
        }
        else
        {
            initials = !firstName.isEmpty() ? firstName.at(0).toUpper() : QString();
            initials += !lastName.isEmpty() ? lastName.at(0).toUpper() : QString();
        }

        if(!initials.isEmpty())
        {
            const QFontMetricsF fm = paint.fontMetrics();
            QRectF brect = fm.boundingRect(initials); brect.moveTopLeft(QPointF(0,0));
            qreal scale = qreal(dim)*0.65/qMax(brect.width(),brect.height());

            paint.save();
            paint.translate(image.rect().center());
            paint.scale(scale, scale);
            paint.translate(-brect.center() + QPointF(0.5,0.5));
            paint.setPen(Qt::white);
            paint.drawText(0, 0, brect.width(), brect.height(), Qt::AlignCenter|Qt::TextDontClip, initials);
            paint.restore();
        }
    }
    else
    {
        QRectF iconRect = image.rect();
        const qreal iconMargin = iconRect.width()*0.1;
        iconRect.adjust(iconMargin, iconMargin, -iconMargin, -iconMargin);
        paint.setRenderHint(QPainter::SmoothPixmapTransform);
        paint.drawImage(iconRect, QImage(":/icons/content/person_outline_inverted.png"));
    }

    paint.end();

    if(size)
        *size = image.size();

    return image;
}

///////////////////////////////////////////////////////////////////////////////

AppFeature::AppFeature(QObject *parent)
    :QObject(parent)
{
    connect(User::instance(), &User::infoChanged, this, &AppFeature::reevaluate);
    connect(User::instance(), &User::loggedInChanged, this, &AppFeature::reevaluate);
}

AppFeature::~AppFeature()
{

}

void AppFeature::setFeatureName(const QString &val)
{
    if(m_featureName == val)
        return;

    m_featureName = val;
    emit featureNameChanged();
    this->reevaluate();
}

void AppFeature::setFeature(int val)
{
    if(m_feature == val)
        return;

    m_feature = val;
    emit featureChanged();
    this->reevaluate();
}

void AppFeature::reevaluate()
{
    if(User::instance()->isLoggedIn())
    {
        const bool flag1 = m_feature < 0 ? true : User::instance()->isFeatureEnabled(User::AppFeature(m_feature));
        const bool flag2 = m_featureName.isEmpty() ? true : User::instance()->isFeatureNameEnabled(m_featureName);
        this->setEnabled(flag1 && flag2);
    }
    else
        this->setEnabled(false);

#ifndef QT_NODEBUG
    qDebug() << "PA: " << m_featureName << "/" << m_feature << " = " << m_enabled;
#endif
}

void AppFeature::setEnabled(bool val)
{
    if(m_enabled == val)
        return;

    m_enabled = val;
    emit enabledChanged();
}
