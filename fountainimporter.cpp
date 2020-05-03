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

#include "fountainimporter.h"

FountainImporter::FountainImporter(QObject *parent)
    : AbstractImporter(parent)
{

}

FountainImporter::~FountainImporter()
{

}

bool FountainImporter::doImport(QIODevice *device)
{
    // Have tried to parse the Fountain file as closely as possible to
    // the syntax described here: https://fountain.io/syntax
    ScriteDocument *doc = this->document();
    Structure *structure = doc->structure();
    Screenplay *screenplay = doc->screenplay();
    static const QList<QColor> colorPalette = QList<QColor>() <<
            QColor("purple") << QColor("blue") << QColor("orange") <<
            QColor("red") << QColor("brown") << QColor("gray");

    int sceneCounter = 0;
    Scene *previousScene = nullptr;
    Scene *currentScene = nullptr;
    Character *character = nullptr;
    static const QStringList headerhints = QStringList() <<
            "INT" << "EXT" << "EST" << "INT./EXT" << "INT/EXT" << "I/E";
    bool inCharacter = false;
    bool hasParaBreaks = false;
    auto maybeCharacter = [](QString &text) {
        if(text.startsWith("@")) {
            text = text.remove(0, 1);
            return true;
        }

        if(text.at(0).script() != QChar::Script_Latin)
            return false;

        for(int i=0; i<text.length(); i++) {
            const QChar ch = text.at(i);
            if(ch.isLetter()) {
                if(ch.isLower())
                    return  false;
            }
        }

        return true;
    };

    const QByteArray bytes = device->readAll();

    QTextStream ts(bytes);
    ts.setCodec("utf-8");
    ts.setAutoDetectUnicode(true);

    while(!ts.atEnd())
    {
        QString line = ts.readLine();
        line = line.trimmed();

        if(line.isEmpty())
        {
            inCharacter = false;
            hasParaBreaks = true;
            character = nullptr;
            continue;
        }

        if(line.startsWith("#"))
        {
            line = line.remove("#").trimmed();
            line = line.split(" ", QString::SkipEmptyParts).first();

            ScreenplayElement *element = new ScreenplayElement(screenplay);
            element->setElementType(ScreenplayElement::BreakElementType);
            element->setSceneFromID(line);
            screenplay->addElement(element);
            continue;
        }

        QString pruned;
        if(line.startsWith('['))
        {
            // Hack to be able to import fountain files with [nnn]
            const int bcIndex = line.indexOf(']');
            pruned = line.left(bcIndex+1);
            line = line.mid(bcIndex+1).trimmed();
        }

        // We do not support other formatting features from the fountain syntax
        line = line.remove("_");
        line = line.remove("*");
        line = line.remove("^");

        // detect if ths line contains a header.
        bool isHeader = false;
        if(!inCharacter)
        {
            const QChar sep = line.at(0);
            if(sep == '.')
                isHeader = true;

            if(isHeader == false)
            {
                Q_FOREACH(QString hint, headerhints)
                {
                    if(line.startsWith(hint))
                    {
                        if(hint == headerhints.first())
                        {
                            isHeader = true;
                            break;
                        }
                    }
                }
            }
        }

        if(isHeader)
        {
            ++sceneCounter;
            screenplay->setCurrentElementIndex(-1);
            previousScene = currentScene;
            currentScene = doc->createNewScene();

            SceneHeading *heading = currentScene->heading();
            if(line.at(0) == QChar('.'))
            {
                line = line.remove(0, 1);
                const int dotIndex = line.indexOf('.');
                const int dashIndex = line.indexOf('-');

                if(dashIndex >= 0)
                {
                    const QString moment = line.mid(dashIndex+1).trimmed();
                    heading->setMoment(moment);
                    line = line.left(dashIndex);
                }
                else
                    heading->setMoment( previousScene ? previousScene->heading()->moment() : "DAY" );

                if(dotIndex >= 0)
                {
                    const QString locType = line.left(dotIndex).trimmed();
                    heading->setLocationType(locType);
                    line = line.remove(0, dotIndex+1);
                }
                else
                    heading->setLocationType( previousScene ? previousScene->heading()->locationType() : "I/E" );

                heading->setLocation(line.trimmed());
            }
            else
                heading->parseFrom(line);

            QString locationForTitle = heading->location();
            if(locationForTitle.length() > 25)
                locationForTitle = locationForTitle.left(22) + "...";

            currentScene->setTitle("[" + QString::number(sceneCounter) + "]: @ " + locationForTitle);
            currentScene->setColor( colorPalette.at( sceneCounter%colorPalette.length()) );
            continue;
        }

        if(!pruned.isEmpty())
            line = pruned + " " + line;

        if(currentScene == nullptr)
        {
            if(line.startsWith("Title:", Qt::CaseInsensitive))
            {
                const QString title = line.section(':',1);
                const int boIndex = title.indexOf('(');
                const int bcIndex = title.lastIndexOf(')');
                if(boIndex >= 0 && bcIndex >= 0)
                {
                    screenplay->setSubtitle(title.mid(boIndex+1, bcIndex-boIndex-1));
                    screenplay->setTitle(title.left(boIndex).trimmed());
                }
                else
                    screenplay->setTitle(title);
            }
            else if(line.startsWith("Author:", Qt::CaseInsensitive))
                screenplay->setAuthor(line.section(':',1));
            else if(line.startsWith("Version:", Qt::CaseInsensitive))
                screenplay->setVersion(line.section(':',1));
            else if(line.startsWith("Contact:", Qt::CaseInsensitive))
                screenplay->setContact(line.section(':',1));
            else if(line.at(0) == QChar('@'))
            {
                line = line.remove(0, 1).trimmed();

                character = structure->findCharacter(line);
                if(character == nullptr)
                    character = new Character(structure);
                character->setName(line.trimmed());
                structure->addCharacter(character);
                continue;
            }
            else if(character != nullptr)
            {
                if(line.startsWith('(') && line.endsWith(')'))
                {
                    line.remove(0, 1);
                    line.remove(line.length()-1, 1);

                    Note *note = new Note(character);
                    note->setHeading(line);
                    character->addNote(note);
                }
                else
                {
                    Note *note = character->noteAt(character->noteCount()-1);
                    if(note == nullptr)
                    {
                        note = new Note(character);
                        note->setHeading("Note");
                        character->addNote(note);
                    }

                    note->setContent(line);
                }
            }

            continue; // ignore lines until we get atleast one heading.
        }

        SceneElement *para = new SceneElement;
        para->setText(line);

        if(line.endsWith("TO:", Qt::CaseInsensitive))
        {
            para->setType(SceneElement::Transition);
            currentScene->addElement(para);
            continue;
        }

        if(line.startsWith(">"))
        {
            line = line.remove(0, 1);
            if(line.endsWith("<"))
                line = line.remove(line.length()-1,1);
            para->setText(line);
            para->setType(SceneElement::Shot);
            currentScene->addElement(para);
            continue;
        }

        if(line.startsWith('(') && line.endsWith(')'))
        {
            if(inCharacter)
            {
                para->setType(SceneElement::Parenthetical);
                currentScene->addElement(para);
                continue;
            }

            // Parenthetical must be provided as a part of character-dialogue
            // construct only. If its free-and-floating, then we will interpret
            // it as Scene notes.
            line = line.remove(0, 1);
            line = line.remove(line.length()-1, 1);
            Note *note = new Note(currentScene);
            note->setHeading("Note #" + QString::number(currentScene->noteCount()+1));
            note->setContent(line);
            note->setColor( colorPalette.at( currentScene->noteCount()%colorPalette.length()) );
            currentScene->addNote(note);

            delete para;
            continue;
        }

        if(!inCharacter && maybeCharacter(line))
        {
            para->setText(line);
            para->setType(SceneElement::Character);
            currentScene->addElement(para);
            inCharacter = true;
            continue;
        }

        if(inCharacter)
        {
            para->setType(SceneElement::Dialogue);
            if(!hasParaBreaks)
                inCharacter = false;
        }
        else
            para->setType(SceneElement::Action);
        currentScene->addElement(para);
    }

    screenplay->setCurrentElementIndex(0);

    return true;
}
