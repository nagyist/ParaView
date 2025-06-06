// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-FileCopyrightText: Copyright (c) Sandia Corporation
// SPDX-License-Identifier: BSD-3-Clause
#include "pqLiveSourceBehavior.h"

#include "pqLiveSourceManager.h"
#include "pqPVApplicationCore.h"

//-----------------------------------------------------------------------------
pqLiveSourceBehavior::pqLiveSourceBehavior(QObject* parentObject)
  : Superclass(parentObject)
{
  pqPVApplicationCore::instance()->instantiateLiveSourceManager();
}

//-----------------------------------------------------------------------------
pqLiveSourceBehavior::~pqLiveSourceBehavior() = default;
