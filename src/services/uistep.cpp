/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/uistep.h"

namespace
{
/// Relaxed is enough: the value is a pointer to a literal and the readers are
/// diagnostics. There is nothing to order it against.
std::atomic<const char *> gStep { nullptr };
}

namespace UiStep
{

const char *currentRaw() { return gStep.load(std::memory_order_relaxed); }

const char *set(const char *step)
{
    return gStep.exchange(step, std::memory_order_relaxed);
}

}   // namespace UiStep
