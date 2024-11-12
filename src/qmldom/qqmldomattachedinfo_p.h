// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QMLDOMATTACHEDINFO_P_H
#define QMLDOMATTACHEDINFO_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include "qqmldom_global.h"
#include "qqmldomitem_p.h"

#include <memory>

QT_BEGIN_NAMESPACE

namespace QQmlJS {
namespace Dom {
namespace FileLocations {

struct Info
{
    constexpr static DomType kindValue = DomType::FileLocationsInfo;
    // mainly used for debugging, for example dumping qmlFile
    bool iterateDirectSubpaths(const DomItem &self, DirectVisitor) const;

    SourceLocation fullRegion;
    QMap<FileLocationRegion, SourceLocation> regions;
};

using Tree = std::shared_ptr<AttachedInfo>;
Tree createTree(const Path &basePath);
Tree ensure(const Tree &base, const Path &basePath);
Tree find(const Tree &self, const Path &p);

bool visitTree(const Tree &base, function_ref<bool(const Path &, const Tree &)> visitor,
               const Path &basePath = Path());

// TODO move to some testing utils
QString canonicalPathForTesting(const Tree &base);

// returns the path looked up and the found tree when looking for the info attached to item
Tree treeOf(const DomItem &);

void updateFullLocation(const Tree &fLoc, SourceLocation loc);
void addRegion(const Tree &fLoc, FileLocationRegion region, SourceLocation loc);
QQmlJS::SourceLocation region(const Tree &fLoc, FileLocationRegion region);

} // namespace FileLocations

class QMLDOM_EXPORT AttachedInfo : public OwningItem,
                                   public std::enable_shared_from_this<AttachedInfo>
{
public:
    constexpr static DomType kindValue = DomType::AttachedInfo;
    using Ptr = std::shared_ptr<AttachedInfo>;

    DomType kind() const override { return kindValue; }
    Path canonicalPath(const DomItem &self) const override { return self.m_ownerPath; }
    // mainly used for debugging, for example dumping qmlFile
    bool iterateDirectSubpaths(const DomItem &self, DirectVisitor visitor) const override;

    AttachedInfo::Ptr makeCopy(const DomItem &self) const
    {
        return std::static_pointer_cast<AttachedInfo>(doCopy(self));
    }

    AttachedInfo(const Ptr &parent = nullptr, const Path &p = Path()) : m_path(p), m_parent(parent)
    {
    }

    AttachedInfo(const AttachedInfo &o) = default;

    Path path() const { return m_path; }
    Ptr parent() const { return m_parent.lock(); }
    QMap<Path, Ptr> subItems() const { return m_subItems; }
    FileLocations::Info &info() { return m_info; }

    void setPath(const Path &p) { m_path = p; }
    Ptr insertOrReturnChildAt(const Path &path);

private:
    std::shared_ptr<OwningItem> doCopy(const DomItem &) const override
    {
        return std::make_shared<AttachedInfo>(*this);
    }

private:
    Path m_path;
    std::weak_ptr<AttachedInfo> m_parent;
    QMap<Path, Ptr> m_subItems;
    FileLocations::Info m_info;
};

} // end namespace Dom
} // end namespace QQmlJS

QT_END_NAMESPACE
#endif // QMLDOMATTACHEDINFO_P_H
