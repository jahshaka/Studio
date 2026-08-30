/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PUBLISHMODULE_H
#define PUBLISHMODULE_H

// PublishModule — the first born-native StudioModule (audit §6.3.3): the
// cheapest possible proof of the module pattern. Today it owns only the
// Publish page stub; the publishing targets (web/glTF export first —
// jahshaka-web-export direction) land inside this module, verbs first.

#include "modules/studiomodule.h"

class PublishModule : public StudioModule
{
public:
    QString id() const override { return QStringLiteral("publish"); }
    void initialize(ModuleHost &host) override { this->host = host; }
    QWidget *createPage() override;
    void shutdown() override {}

private:
    ModuleHost host;
};

#endif // PUBLISHMODULE_H
