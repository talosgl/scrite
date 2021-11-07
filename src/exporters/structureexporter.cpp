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

#include "structureexporter.h"
#include "structureexporter_p.h"

#include "application.h"

#include <QDir>
#include <QPainter>
#include <QFileInfo>
#include <QPdfWriter>
#include <QPainterPath>
#include <QAbstractTextDocumentLayout>

StructureExporter::StructureExporter(QObject *parent)
    :AbstractExporter(parent)
{

}

StructureExporter::~StructureExporter()
{

}

void StructureExporter::setInsertTitleCard(bool val)
{
    if(m_insertTitleCard == val)
        return;

    m_insertTitleCard = val;
    emit insertTitleCardChanged();
}

void StructureExporter::setEnableHeaderFooter(bool val)
{
    if(m_enableHeaderFooter == val)
        return;

    m_enableHeaderFooter = val;
    emit enableHeaderFooterChanged();
}

void StructureExporter::setWatermark(const QString &val)
{
    if(m_watermark == val)
        return;

    m_watermark = val;
    emit watermarkChanged();
}

void StructureExporter::setComment(const QString &val)
{
    if(m_comment == val)
        return;

    m_comment = val;
    emit commentChanged();
}

bool StructureExporter::doExport(QIODevice *device)
{
    const Screenplay *screenplay = this->document()->screenplay();
    const Structure *structure = this->document()->structure();

    if(structure->canvasUIMode() != Structure::IndexCardUI)
    {
        this->error()->setErrorMessage( QStringLiteral("Only index card based structures can be exported.") );
        return false;
    }

    // Construct the graphics scene with content of the structure
    StructureExporterScene scene(this);
    scene.setTitle(screenplay->title() + QStringLiteral(" - Structure"));
    return scene.exportToPdf(device);
}

QString StructureExporter::polishFileName(const QString &fileName) const
{
    QFileInfo fi(fileName);

    QString baseName = fi.baseName();
    baseName.replace( QStringLiteral("Screenplay"), QStringLiteral("Structure") );

    return fi.absoluteDir().absoluteFilePath( baseName + QStringLiteral(".pdf") );
}

