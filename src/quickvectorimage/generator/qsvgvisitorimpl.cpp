// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include "qsvgvisitorimpl_p.h"
#include "qquickgenerator_p.h"
#include "qquicknodeinfo_p.h"

#include <private/qsvgvisitor_p.h>

#include <QString>
#include <QPainter>
#include <QTextDocument>
#include <QTextLayout>
#include <QMatrix4x4>
#include <QQuickItem>

#include <private/qquickshape_p.h>
#include <private/qquicktext_p.h>
#include <private/qquicktranslate_p.h>
#include <private/qquickitem_p.h>

#include <private/qquickimagebase_p_p.h>
#include <private/qquickimage_p.h>
#include <private/qsgcurveprocessor_p.h>

#include <private/qquadpath_p.h>

#include "utils_p.h"
#include <QtCore/qloggingcategory.h>

QT_BEGIN_NAMESPACE

using namespace Qt::StringLiterals;

Q_DECLARE_LOGGING_CATEGORY(lcQuickVectorImage)

static inline bool isPathContainer(const QSvgStructureNode *node)
{
    bool foundPath = false;
    for (const auto *child : node->renderers()) {
        switch (child->type()) {
            // nodes that shouldn't go inside Shape{}
        case QSvgNode::Switch:
        case QSvgNode::Doc:
        case QSvgNode::Group:
        case QSvgNode::Animation:
        case QSvgNode::Use:
        case QSvgNode::Video:
            //qCDebug(lcQuickVectorGraphics) << "NOT path container because" << node->typeName() ;
            return false;

            // nodes that could go inside Shape{}
        case QSvgNode::Defs:
        case QSvgNode::Image:
        case QSvgNode::Textarea:
        case QSvgNode::Text:
        case QSvgNode::Tspan:
            break;

            // nodes that are done as pure ShapePath{}
        case QSvgNode::Rect:
        case QSvgNode::Circle:
        case QSvgNode::Ellipse:
        case QSvgNode::Line:
        case QSvgNode::Path:
        case QSvgNode::Polygon:
        case QSvgNode::Polyline:
            if (!child->style().transform.isDefault()) {
                //qCDebug(lcQuickVectorGraphics) << "NOT path container because local transform";
                return false;
            }
            foundPath = true;
            break;
        default:
            qCDebug(lcQuickVectorImage) << "Unhandled type in switch" << child->type();
            break;
        }
    }
    //qCDebug(lcQuickVectorGraphics) << "Container" << node->nodeId() << node->typeName()  << "is" << foundPath;
    return foundPath;
}

class QSvgStyleResolver
{
public:
    QSvgStyleResolver()
    {
        m_dummyImage = QImage(1, 1, QImage::Format_RGB32);
        m_dummyPainter.begin(&m_dummyImage);
        QPen noPen(Qt::NoPen);
        noPen.setBrush(Qt::NoBrush);
        m_dummyPainter.setPen(noPen);
        m_dummyPainter.setBrush(Qt::black);
    }

    ~QSvgStyleResolver()
    {
        m_dummyPainter.end();
    }

    QPainter& painter() { return m_dummyPainter; }
    QSvgExtraStates& states() { return m_svgState; }

    QString currentFillColor() const
    {
        if (m_dummyPainter.brush().style() != Qt::NoBrush) {
            QColor c(m_dummyPainter.brush().color());
            c.setAlphaF(m_svgState.fillOpacity);
            //qCDebug(lcQuickVectorGraphics) << "FILL" << c << m_svgState.fillOpacity << c.name();
            return c.name(QColor::HexArgb);
        } else {
            return QStringLiteral("transparent");
        }
    }

    const QGradient *currentFillGradient() const
    {
        if (m_dummyPainter.brush().style() == Qt::LinearGradientPattern || m_dummyPainter.brush().style() == Qt::RadialGradientPattern || m_dummyPainter.brush().style() == Qt::ConicalGradientPattern )
            return m_dummyPainter.brush().gradient();
        return nullptr;
    }

    QString currentStrokeColor() const
    {
        if (m_dummyPainter.pen().style() != Qt::NoPen)
            return m_dummyPainter.pen().color().name();
        else if (m_dummyPainter.pen().brush().style() == Qt::SolidPattern)
            return m_dummyPainter.pen().brush().color().name();
        return {};
    }

    float currentStrokeWidth() const
    {
        float penWidth = m_dummyPainter.pen().widthF();
        return penWidth ? penWidth : 1;
    }

protected:
    QPainter m_dummyPainter;
    QImage m_dummyImage;
    QSvgExtraStates m_svgState;

};

Q_GLOBAL_STATIC(QSvgStyleResolver, styleResolver)

QSvgVisitorImpl::QSvgVisitorImpl(const QString svgFileName, QQuickGenerator *generator)
    : m_svgFileName(svgFileName)
    , m_generator(generator)
{

}

void QSvgVisitorImpl::traverse()
{
    if (!m_generator) {
        qCDebug(lcQuickVectorImage) << "No valid QQuickGenerator is set. Genration will stop";
        return;
    }

    auto *doc = QSvgTinyDocument::load(m_svgFileName);
    if (!doc) {
        qCDebug(lcQuickVectorImage) << "Not a valid Svg File : " << m_svgFileName;
        return;
    }

    QSvgVisitor::traverse(doc);
}

void QSvgVisitorImpl::visitNode(const QSvgNode *node)
{
    handleBaseNodeSetup(node);

    ImageNodeInfo info;
    fillCommonNodeInfo(node, info);

    m_generator->generateNode(info);

    handleBaseNodeEnd(node);
}

void QSvgVisitorImpl::visitImageNode(const QSvgImage *node)
{
    // TODO: this requires proper asset management.
    handleBaseNodeSetup(node);

    ImageNodeInfo info;
    fillCommonNodeInfo(node, info);
    info.image = node->image();
    info.rect = node->rect();

    m_generator->generateImageNode(info);

    handleBaseNodeEnd(node);
}

void QSvgVisitorImpl::visitRectNode(const QSvgRect *node)
{
    QRectF rect = node->rect();
    QPointF rads = node->radius();
    // This is using Qt::RelativeSize semantics: percentage of half rect size
    qreal x1 = rect.left();
    qreal x2 = rect.right();
    qreal y1 = rect.top();
    qreal y2 = rect.bottom();

    qreal rx =  rads.x() * rect.width() / 200;
    qreal ry = rads.y() * rect.height() / 200;
    QPainterPath p;

    p.moveTo(x1 + rx, y1);
    p.lineTo(x2 - rx, y1);
    // qCDebug(lcQuickVectorGraphics) << "Line1" << x2 - rx << y1;
    p.arcTo(x2 - rx * 2, y1, rx * 2, ry * 2, 90, -90); // ARC to x2, y1 + ry
    // qCDebug(lcQuickVectorGraphics) << "p1" << p;

    p.lineTo(x2, y2 - ry);
    p.arcTo(x2 - rx * 2, y2 - ry * 2, rx * 2, ry * 2, 0, -90); // ARC to x2 - rx, y2

    p.lineTo(x1 + rx, y2);
    p.arcTo(x1, y2 - ry * 2, rx * 2, ry * 2, 270, -90); // ARC to x1, y2 - ry

    p.lineTo(x1, y1 + ry);
    p.arcTo(x1, y1, rx * 2, ry * 2, 180, -90); // ARC to x1 + rx, y1


    handlePathNode(node, p);

    return;
}

void QSvgVisitorImpl::visitEllipseNode(const QSvgEllipse *node)
{
    QRectF rect = node->rect();

    QPainterPath p;
    p.addEllipse(rect);

    handlePathNode(node, p);
}

void QSvgVisitorImpl::visitPathNode(const QSvgPath *node)
{
    handlePathNode(node, node->path());
}

void QSvgVisitorImpl::visitLineNode(const QSvgLine *node)
{
    // TODO: proper end caps (should be flat by default?)
    QPainterPath p;
    p.moveTo(node->line().p1());
    p.lineTo(node->line().p2());
    handlePathNode(node, p, Qt::FlatCap);
}

void QSvgVisitorImpl::visitPolygonNode(const QSvgPolygon *node)
{
    QPainterPath p = QQuickVectorImageGenerator::Utils::polygonToPath(node->polygon(), true);
    handlePathNode(node, p);
}

void QSvgVisitorImpl::visitPolylineNode(const QSvgPolyline *node)
{
    QPainterPath p = QQuickVectorImageGenerator::Utils::polygonToPath(node->polygon(), false);
    handlePathNode(node, p, Qt::FlatCap);
}

QString QSvgVisitorImpl::gradientCssDescription(const QGradient *gradient)
{
    QString cssDescription;
    if (gradient->type() == QGradient::LinearGradient) {
        const QLinearGradient *linearGradient = static_cast<const QLinearGradient *>(gradient);

        cssDescription += " -qt-foreground: qlineargradient("_L1;
        cssDescription += "x1:"_L1 + QString::number(linearGradient->start().x()) + u',';
        cssDescription += "y1:"_L1 + QString::number(linearGradient->start().y()) + u',';
        cssDescription += "x2:"_L1 + QString::number(linearGradient->finalStop().x()) + u',';
        cssDescription += "y2:"_L1 + QString::number(linearGradient->finalStop().y()) + u',';
    } else if (gradient->type() == QGradient::RadialGradient) {
        const QRadialGradient *radialGradient = static_cast<const QRadialGradient *>(gradient);

        cssDescription += " -qt-foreground: qradialgradient("_L1;
        cssDescription += "cx:"_L1 + QString::number(radialGradient->center().x()) + u',';
        cssDescription += "cy:"_L1 + QString::number(radialGradient->center().y()) + u',';
        cssDescription += "fx:"_L1 + QString::number(radialGradient->focalPoint().x()) + u',';
        cssDescription += "fy:"_L1 + QString::number(radialGradient->focalPoint().y()) + u',';
        cssDescription += "radius:"_L1 + QString::number(radialGradient->radius()) + u',';
    } else {
        const QConicalGradient *conicalGradient = static_cast<const QConicalGradient *>(gradient);

        cssDescription += " -qt-foreground: qconicalgradient("_L1;
        cssDescription += "cx:"_L1 + QString::number(conicalGradient->center().x()) + u',';
        cssDescription += "cy:"_L1 + QString::number(conicalGradient->center().y()) + u',';
        cssDescription += "angle:"_L1 + QString::number(conicalGradient->angle()) + u',';
    }

    const QStringList coordinateModes = { "logical"_L1, "stretchtodevice"_L1, "objectbounding"_L1, "object"_L1 };
    cssDescription += "coordinatemode:"_L1;
    cssDescription += coordinateModes.at(int(gradient->coordinateMode()));
    cssDescription += u',';

    const QStringList spreads = { "pad"_L1, "reflect"_L1, "repeat"_L1 };
    cssDescription += "spread:"_L1;
    cssDescription += spreads.at(int(gradient->spread()));

    for (const QGradientStop &stop : gradient->stops()) {
        cssDescription += ",stop:"_L1;
        cssDescription += QString::number(stop.first);
        cssDescription += u' ';
        cssDescription += stop.second.name();
    }

    cssDescription += ");"_L1;

    return cssDescription;
}

void QSvgVisitorImpl::visitTextNode(const QSvgText *node)
{
    handleBaseNodeSetup(node);
    const bool isTextArea = node->type() == QSvgNode::Textarea;

    QString text;
    bool needsRichText = false;
    bool preserveWhiteSpace = node->whitespaceMode() == QSvgText::Preserve;
    const QGradient *mainGradient = styleResolver->currentFillGradient();
    bool needsPathNode = mainGradient != nullptr;
    for (const auto *tspan : node->tspans()) {
        if (!tspan) {
            text += QStringLiteral("<br>");
            continue;
        }

        // Note: We cannot get the font directly from the style, since this does
        // not apply the weight, since this is relative and depends on current state.
        handleBaseNodeSetup(tspan);
        QFont font = styleResolver->painter().font();

        QString styleTagContent;

        if ((font.resolveMask() & QFont::FamilyResolved)
            || (font.resolveMask() & QFont::FamiliesResolved)) {
            styleTagContent += QStringLiteral("font-family: %1;").arg(font.family());
        }

        if (font.resolveMask() & QFont::WeightResolved
            && font.weight() != QFont::Normal
            && font.weight() != QFont::Bold) {
            styleTagContent += QStringLiteral("font-weight: %1;").arg(int(font.weight()));
        }

        if (font.resolveMask() & QFont::SizeResolved) {
            // Pixel size stored as point size in SVG parser
            styleTagContent += QStringLiteral("font-size: %1px;").arg(int(font.pointSizeF()));
        }

        if (font.resolveMask() & QFont::CapitalizationResolved
            && font.capitalization() == QFont::SmallCaps) {
            styleTagContent += QStringLiteral("font-variant: small-caps;");
        }

        if (styleResolver->currentFillGradient() != nullptr
            && styleResolver->currentFillGradient() != mainGradient) {
            styleTagContent += gradientCssDescription(styleResolver->currentFillGradient()) + u';';
            needsPathNode = true;
        }

        if (tspan->whitespaceMode() == QSvgText::Preserve && !preserveWhiteSpace)
            styleTagContent += QStringLiteral("white-space: pre-wrap;");

        QString content = tspan->text().toHtmlEscaped();
        content.replace(QLatin1Char('\t'), QLatin1Char(' '));
        content.replace(QLatin1Char('\n'), QLatin1Char(' '));

        needsRichText = needsRichText || !styleTagContent.isEmpty();
        if (!styleTagContent.isEmpty())
            text += QStringLiteral("<span style=\"%1\">").arg(styleTagContent);

        if (font.resolveMask() & QFont::WeightResolved && font.bold())
            text += QStringLiteral("<b>");

        if (font.resolveMask() & QFont::StyleResolved && font.italic())
            text += QStringLiteral("<i>");

        QString spanColor;
        if (!tspan->style().fill.isDefault()) {
            auto &b = tspan->style().fill->qbrush();
            qCDebug(lcQuickVectorImage) << "tspan FILL:" << b;
            if (b.style() != Qt::NoBrush)
                spanColor = b.color().name();
        }
        bool fontTag = !spanColor.isEmpty();
        if (fontTag)
            text += QStringLiteral("<font color=\"%1\">").arg(spanColor);

        if (font.resolveMask() & QFont::CapitalizationResolved) {
            switch (font.capitalization()) {
            case QFont::AllLowercase:
                content = content.toLower();
                break;
            case QFont::AllUppercase:
                content = content.toUpper();
                break;
            case QFont::Capitalize:
                // ### We need to iterate over the string and do the title case conversion,
                // since this is not part of QString.
                qCWarning(lcQuickVectorImage) << "Title case not implemented for tspan";
                break;
            default:
                break;
            }
        }
        text += content;
        if (fontTag)
            text += QStringLiteral("</font>");

        if (font.resolveMask() & QFont::StyleResolved && font.italic())
            text += QStringLiteral("</i>");

        if (font.resolveMask() & QFont::WeightResolved && font.bold())
            text += QStringLiteral("</b>");

        if (!styleTagContent.isEmpty())
            text += QStringLiteral("</span>");

        handleBaseNodeEnd(tspan);
    }

    if (preserveWhiteSpace && (needsRichText || styleResolver->currentFillGradient() != nullptr))
        text = QStringLiteral("<span style=\"white-space: pre-wrap\">") + text + QStringLiteral("</span>");

    QFont font = styleResolver->painter().font();
    if (font.pixelSize() <= 0 && font.pointSize() > 0)
        font.setPixelSize(font.pointSize()); // Pixel size stored as point size by SVG parser

    if (needsPathNode) {
        QPainterPath path;

        QTextDocument document;
        document.setHtml(text);
        if (isTextArea && node->size().width() > 0)
            document.setTextWidth(node->size().width());
        document.setDefaultFont(font);
        document.pageCount(); // Force layout

        QTextBlock block = document.firstBlock();
        while (block.isValid()) {
            QTextLayout *lout = block.layout();

            if (lout != nullptr) {
                QTextCharFormat currentFormat;

                auto addPathForFormat = [&](QPainterPath p, QTextCharFormat fmt) {
                    PathNodeInfo info;
                    fillCommonNodeInfo(node, info);
                    auto fillStyle = node->style().fill;
                    if (fillStyle)
                        info.fillRule = fillStyle->fillRule();

                    if (fmt.hasProperty(QTextCharFormat::ForegroundBrush)) {
                        info.fillColor = fmt.foreground().color().name();
                        if (fmt.foreground().gradient() != nullptr && fmt.foreground().gradient()->type() != QGradient::NoGradient)
                            info.grad = *fmt.foreground().gradient();
                    } else {
                        info.fillColor = styleResolver->currentFillColor();
                    }

                    info.painterPath = p;
                    info.strokeColor = styleResolver->currentStrokeColor();
                    info.strokeWidth = styleResolver->currentStrokeWidth();
                    if (info.grad.type() == QGradient::NoGradient && styleResolver->currentFillGradient() != nullptr)
                        info.grad = *styleResolver->currentFillGradient();

                    m_generator->generatePath(info);
                };

                qreal baselineOffset = -QFontMetricsF(font).ascent();
                if (lout->lineCount() > 0 && lout->lineAt(0).isValid())
                    baselineOffset = -lout->lineAt(0).ascent();

                const QPointF baselineTranslation(0.0, baselineOffset);
                auto glyphsToPath = [&](QList<QGlyphRun> glyphRuns) {
                    QPainterPath path;
                    path.setFillRule(Qt::WindingFill);
                    for (const QGlyphRun &glyphRun : glyphRuns) {
                        QRawFont font = glyphRun.rawFont();
                        QList<quint32> glyphIndexes = glyphRun.glyphIndexes();
                        QList<QPointF> positions = glyphRun.positions();

                        for (qsizetype j = 0; j < glyphIndexes.size(); ++j) {
                            quint32 glyphIndex = glyphIndexes.at(j);
                            const QPointF &pos = positions.at(j);

                            QPainterPath p = font.pathForGlyph(glyphIndex);
                            p.translate(pos + node->position() + baselineTranslation);
                            path.addPath(p);
                        }
                    }

                    return path;
                };

                QList<QTextLayout::FormatRange> formats = block.textFormats();
                for (int i = 0; i < formats.size(); ++i) {
                    QTextLayout::FormatRange range = formats.at(i);

                    // If we hit a "multi" anchor, it means we have additional formats to apply
                    // for both this and the subsequent range, so we merge them.
                    if (!range.format.anchorNames().isEmpty()
                        && range.format.anchorNames().first().startsWith(QStringLiteral("multi"))
                        && i < formats.size() - 1) {
                        QTextLayout::FormatRange nextRange = formats.at(++i);
                        range.length += nextRange.length;
                        range.format.merge(nextRange.format);
                    }
                    QList<QGlyphRun> glyphRuns = lout->glyphRuns(range.start, range.length);
                    QPainterPath path = glyphsToPath(glyphRuns);
                    addPathForFormat(path, range.format);
                }
            }

            block = block.next();
        }
    } else {
        TextNodeInfo info;
        fillCommonNodeInfo(node, info);

        info.position = node->position();
        info.size = node->size();
        info.font = font;
        info.text = text;
        info.isTextArea = isTextArea;
        info.needsRichText = needsRichText;
        info.color = styleResolver->currentFillColor();
        info.alignment = styleResolver->states().textAnchor;
        info.strokeColor = styleResolver->currentStrokeColor();

        m_generator->generateTextNode(info);
    }

    handleBaseNodeEnd(node);
}

void QSvgVisitorImpl::visitUseNode(const QSvgUse *node)
{
    QSvgNode *link = node->link();
    if (!link)
        return;

    handleBaseNodeSetup(node);
    UseNodeInfo info;
    fillCommonNodeInfo(node, info);

    info.stage = StructureNodeStage::Start;
    info.startPos = node->start();

    m_generator->generateUseNode(info);

    QSvgVisitor::traverse(link);

    info.stage = StructureNodeStage::End;
    m_generator->generateUseNode(info);
    handleBaseNodeEnd(node);
}

bool QSvgVisitorImpl::visitDefsNodeStart(const QSvgDefs *node)
{
    Q_UNUSED(node)

    return m_generator->generateDefsNode(NodeInfo{});
}

bool QSvgVisitorImpl::visitStructureNodeStart(const QSvgStructureNode *node)
{
    constexpr bool forceSeparatePaths = false;
    handleBaseNodeSetup(node);

    StructureNodeInfo info;

    fillCommonNodeInfo(node, info);
    info.forceSeparatePaths = forceSeparatePaths;
    info.isPathContainer = isPathContainer(node);
    info.stage = StructureNodeStage::Start;

    return m_generator->generateStructureNode(info);;
}

void QSvgVisitorImpl::visitStructureNodeEnd(const QSvgStructureNode *node)
{
    handleBaseNodeEnd(node);
    //    qCDebug(lcQuickVectorGraphics) << "REVERT" << node->nodeId() << node->type() << (m_styleResolver->painter().pen().style() != Qt::NoPen) << m_styleResolver->painter().pen().color().name()
    //             << (m_styleResolver->painter().pen().brush().style() != Qt::NoBrush) << m_styleResolver->painter().pen().brush().color().name();

    StructureNodeInfo info;
    fillCommonNodeInfo(node, info);
    info.stage = StructureNodeStage::End;

    m_generator->generateStructureNode(info);
}

bool QSvgVisitorImpl::visitDocumentNodeStart(const QSvgTinyDocument *node)
{
    handleBaseNodeSetup(node);

    StructureNodeInfo info;
    fillCommonNodeInfo(node, info);

    const QSvgTinyDocument *doc = static_cast<const QSvgTinyDocument *>(node);
    info.size = doc->size();
    info.viewBox = doc->viewBox();
    info.isPathContainer = isPathContainer(node);
    info.stage = StructureNodeStage::Start;

    return m_generator->generateRootNode(info);;
}

void QSvgVisitorImpl::visitDocumentNodeEnd(const QSvgTinyDocument *node)
{
    handleBaseNodeEnd(node);
    qCDebug(lcQuickVectorImage) << "REVERT" << node->nodeId() << node->type() << (styleResolver->painter().pen().style() != Qt::NoPen)
                                   << styleResolver->painter().pen().color().name() << (styleResolver->painter().pen().brush().style() != Qt::NoBrush)
                                   << styleResolver->painter().pen().brush().color().name();

    StructureNodeInfo info;
    fillCommonNodeInfo(node, info);
    info.stage = StructureNodeStage::End;

    m_generator->generateRootNode(info);
}

void QSvgVisitorImpl::fillCommonNodeInfo(const QSvgNode *node, NodeInfo &info)
{
    info.nodeId = node->nodeId();
    info.typeName = node->typeName();
    info.isDefaultTransform = node->style().transform.isDefault();
    info.transform = !info.isDefaultTransform ? node->style().transform->qtransform() : QTransform();
    info.isDefaultOpacity = node->style().opacity.isDefault();
    info.opacity = !info.isDefaultOpacity ? node->style().opacity->opacity() : 1.0;
    info.isVisible = node->isVisible();
    info.isDisplayed = node->displayMode() != QSvgNode::DisplayMode::NoneMode;
}

void QSvgVisitorImpl::handleBaseNodeSetup(const QSvgNode *node)
{
    qCDebug(lcQuickVectorImage) << "Before SETUP" << node << "fill" << styleResolver->currentFillColor()
                                   << "stroke" << styleResolver->currentStrokeColor() << styleResolver->currentStrokeWidth()
                                   << node->nodeId() << " type: " << node->typeName()  << " " << node->type();

    node->applyStyle(&styleResolver->painter(), styleResolver->states());

    qCDebug(lcQuickVectorImage) << "After SETUP" << node << "fill" << styleResolver->currentFillColor()
                                   << "stroke" << styleResolver->currentStrokeColor()
                                   << styleResolver->currentStrokeWidth() << node->nodeId();
}

void QSvgVisitorImpl::handleBaseNode(const QSvgNode *node)
{
    NodeInfo info;
    fillCommonNodeInfo(node, info);

    m_generator->generateNodeBase(info);
}

void QSvgVisitorImpl::handleBaseNodeEnd(const QSvgNode *node)
{
    node->revertStyle(&styleResolver->painter(), styleResolver->states());

    qCDebug(lcQuickVectorImage) << "After END" << node << "fill" << styleResolver->currentFillColor()
                                   << "stroke" << styleResolver->currentStrokeColor() << styleResolver->currentStrokeWidth()
                                   << node->nodeId();

}

void QSvgVisitorImpl::handlePathNode(const QSvgNode *node, const QPainterPath &path, Qt::PenCapStyle capStyle)
{
    handleBaseNodeSetup(node);

    PathNodeInfo info;
    fillCommonNodeInfo(node, info);
    auto fillStyle = node->style().fill;
    if (fillStyle)
        info.fillRule = fillStyle->fillRule();

    info.painterPath = path;
    info.capStyle = capStyle;
    info.fillColor = styleResolver->currentFillColor();
    info.strokeColor = styleResolver->currentStrokeColor();
    info.strokeWidth = styleResolver->currentStrokeWidth();
    if (styleResolver->currentFillGradient() != nullptr)
        info.grad = *styleResolver->currentFillGradient();

    m_generator->generatePath(info);

    handleBaseNodeEnd(node);
}

QT_END_NAMESPACE
