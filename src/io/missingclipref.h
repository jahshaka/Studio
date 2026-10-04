/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MISSINGCLIPREF_H
#define MISSINGCLIPREF_H

#include <QString>

/// A skeletal clip a reader could not resolve (CLIP-REF-1): the truth the
/// `clip.missing` issue tells — which clip, which asset, why.
struct MissingClipRef
{
    QString clipName;   ///< the clip's name within its asset
    QString assetGuid;  ///< the reference (empty: the scene names no asset)
    QString assetName;  ///< the catalog row's name, when there is a row
    QString nodeName;   ///< the node that holds the clip
    QString why;        ///< the reason, in the user's words
};

#endif // MISSINGCLIPREF_H
