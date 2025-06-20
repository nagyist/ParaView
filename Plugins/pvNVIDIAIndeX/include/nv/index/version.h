/******************************************************************************
 * Copyright 2025 NVIDIA Corporation. All rights reserved.
 *****************************************************************************/
/// \file
/// \brief NVIDIA IndeX version information.

#ifndef NVIDIA_INDEX_VERSION_H
#define NVIDIA_INDEX_VERSION_H

/// NVIDIA IndeX library major and minor version number without qualifier in a string
/// representation, such as \c "2.0".
#define NVIDIA_INDEX_LIBRARY_VERSION_STRING "2.4"

/// NVIDIA IndeX library major and minor version number with qualifier in a string representation,
/// such as \c "2.0" or \c "2.0-beta2".
#define NVIDIA_INDEX_LIBRARY_VERSION_QUALIFIED_STRING "2.4"

/// NVIDIA IndeX library major version.
#define NVIDIA_INDEX_LIBRARY_VERSION_MAJOR 2

/// NVIDIA IndeX library minor version.
#define NVIDIA_INDEX_LIBRARY_VERSION_MINOR 4

/// NVIDIA IndeX library version qualifier.
#define NVIDIA_INDEX_LIBRARY_VERSION_QUALIFIER ""

/// Revision indicating the build of the NVIDIA IndeX library that corresponds to this header file.
/// This string may consist of multiples numbers separated by dots.
#define NVIDIA_INDEX_LIBRARY_REVISION_STRING "385200.2327"

/// Major (branch) number of NVIDIA IndeX revision.
#define NVIDIA_INDEX_LIBRARY_REVISION_MAJOR 385200

/// Minor number of NVIDIA IndeX revision. May be 0 if there is no minor revision.
#define NVIDIA_INDEX_LIBRARY_REVISION_MINOR 2327

/// Sub-minor number of NVIDIA IndeX revision. May be 0 if there is no sub-minor revision (which is
/// commonly the case).
#define NVIDIA_INDEX_LIBRARY_REVISION_SUBMINOR 0

#endif // NVIDIA_INDEX_VERSION_H
