// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
#include "qqmldom_fwd_p.h"
#include "qqmldompath_p.h"
#include "qqmldomattachedinfo_p.h"

QT_BEGIN_NAMESPACE
namespace QQmlJS {
namespace Dom {

using namespace Qt::StringLiterals;

/*!
\internal
\namespace QQmlJS::Dom::FileLocations
\brief Provides entities to maintain mappings between elements and their location in a file

The location information is associated with the element it refers to via AttachedInfo
There are free functions to simplify the handling of the tree of AttachedInfo.

Attributes:
\list
\li fullRegion: A location guaranteed to include this element, its comments, and all its sub
elements \li regions: a map with locations of regions of this element, the empty string is the
default region of this element \endlist
*/
namespace FileLocations {

/*!
\internal
\namespace QQmlJS::Dom::FileLocations::Info
\brief Contains region information about the item

Attributes:
\list
\li fullRegion: A location guaranteed to include this element and all its sub elements
\li regions: a map with locations of regions of this element, the empty string is the default region
 of this element
\endlist
*/
bool Info::iterateDirectSubpaths(const DomItem &self, DirectVisitor visitor) const
{
    bool cont = true;
    cont = cont && self.dvValueLazyField(visitor, Fields::fullRegion, [this]() {
        return sourceLocationToQCborValue(fullRegion);
    });
    cont = cont && self.dvItemField(visitor, Fields::regions, [this, &self]() -> DomItem {
        const Path pathFromOwner = self.pathFromOwner().field(Fields::regions);
        auto map = Map::fromFileRegionMap(pathFromOwner, regions);
        return self.subMapItem(map);
    });
    return cont;
}

Tree createTree(const Path &basePath)
{
    return std::make_shared<AttachedInfo>(nullptr, basePath);
}

Tree ensure(const Tree &base, const Path &basePath)
{
    Path relative = basePath;
    Tree res = base;
    for (const auto &p : std::as_const(relative)) {
        res = res->insertOrReturnChildAt(p);
    }
    return res;
}

Tree find(const Tree &self, const Path &p)
{
    Path rest = p;
    Tree res = self;
    while (rest) {
        if (!res)
            break;
        res = res->subItems().value(rest.head());
        rest = rest.dropFront();
    }
    return res;
}

bool visitTree(const Tree &base, function_ref<bool(const Path &, const Tree &)> visitor,
               const Path &basePath)
{
    if (base) {
        Path pNow = basePath.path(base->path());
        if (!visitor(pNow, base)) {
            return false;
        }
        for (const auto &childNode : base->subItems()) {
            if (!visitTree(childNode, visitor, pNow))
                return false;
        }
    }
    return true;
}

QString canonicalPathForTesting(const Tree &base)
{
    QString result;
    for (auto *it = base.get(); it; it = it->parent().get()) {
        result.prepend(it->path().toString());
    }
    return result;
}

/*!
   \internal
   Returns the tree corresponding to a DomItem.
 */
Tree treeOf(const DomItem &item)
{
    Path p;
    DomItem fLoc = item.field(Fields::fileLocationsTree);
    if (!fLoc) {
        // owner or container.owner should be a file, so this works, but we could simply use the
        // canonical path, and PathType::Canonical instead...
        DomItem o = item.owner();
        p = item.pathFromOwner();
        fLoc = o.field(Fields::fileLocationsTree);
        while (!fLoc && o) {
            DomItem c = o.container();
            p = c.pathFromOwner().path(o.canonicalPath().last()).path(p);
            o = c.owner();
            fLoc = o.field(Fields::fileLocationsTree);
        }
    }
    if (AttachedInfo::Ptr fLocPtr = fLoc.ownerAs<AttachedInfo>())
        return find(fLocPtr, p);
    return {};
}

void updateFullLocation(const Tree &fLoc, SourceLocation loc)
{
    Q_ASSERT(fLoc);
    if (loc != SourceLocation()) {
        Tree p = fLoc;
        while (p) {
            SourceLocation &l = p->info().fullRegion;
            if (loc.begin() < l.begin() || loc.end() > l.end()) {
                l = combine(l, loc);
                p->info().regions[MainRegion] = l;
            } else {
                break;
            }
            p = p->parent();
        }
    }
}

// Adding a new region to file location regions might break down qmlformat because
// comments might be linked to new region undesirably. We might need to add an
// exception to AstRangesVisitor::shouldSkipRegion when confronted those cases.
void addRegion(const Tree &fLoc, FileLocationRegion region, SourceLocation loc)
{
    Q_ASSERT(fLoc);
    fLoc->info().regions[region] = loc;
    updateFullLocation(fLoc, loc);
}

SourceLocation region(const Tree &fLoc, FileLocationRegion region)
{
    Q_ASSERT(fLoc);
    const auto &regions = fLoc->info().regions;
    if (auto it = regions.constFind(region); it != regions.constEnd() && it->isValid()) {
        return *it;
    }

    if (region == MainRegion)
        return fLoc->info().fullRegion;

    return SourceLocation{};
}
} // namespace FileLocations

/*!
\internal
\class QQmlJS::Dom::AttachedInfo
\brief Attached info creates a tree to attach extra info to DomItems

Attributes:
\list
\li parent: parent AttachedInfo in tree (might be empty)
\li subItems: subItems of the tree (path -> AttachedInfo)
\li infoItem: the attached information
\endlist

\sa QQmlJs::Dom::AttachedInfo
*/

bool AttachedInfo::iterateDirectSubpaths(const DomItem &self, DirectVisitor visitor) const
{
    bool cont = true;
    if (Ptr p = parent())
        cont = cont && self.dvItemField(visitor, Fields::parent, [&self, p]() {
            return self.copy(p, self.m_ownerPath.dropTail(2), p.get());
        });
    cont = cont
            && self.dvValueLazyField(visitor, Fields::path, [this]() { return path().toString(); });
    cont = cont && self.dvItemField(visitor, Fields::subItems, [this, &self]() {
        return self.subMapItem(Map(
                Path::Field(Fields::subItems),
                [this](const DomItem &map, const QString &key) {
                    Path p = Path::fromString(key);
                    return map.copy(m_subItems.value(p), map.canonicalPath().key(key));
                },
                [this](const DomItem &) {
                    QSet<QString> res;
                    for (const auto &p : m_subItems.keys())
                        res.insert(p.toString());
                    return res;
                },
                QLatin1String("AttachedInfo")));
    });
    cont = cont && self.dvItemField(visitor, Fields::infoItem, [&self, this]() {
        return self.wrapField(Fields::infoItem, m_info);
    });
    return cont;
}

AttachedInfo::Ptr AttachedInfo::insertOrReturnChildAt(const Path &path)
{
    if (AttachedInfo::Ptr subEl = m_subItems.value(path)) {
        return subEl;
    }
    return m_subItems.insert(path, std::make_shared<AttachedInfo>(shared_from_this(), path))
            .value();
}
} // namespace Dom
} // namespace QQmlJS
QT_END_NAMESPACE

#include "moc_qqmldomattachedinfo_p.cpp"
