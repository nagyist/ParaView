r"""simple is a module for using paraview server manager in Python. It
provides a simple convenience layer to functionality provided by the
C++ classes wrapped to Python as well as the servermanager module.

A simple example::

.. code:: python

    from paraview.simple import *

    # Create a new sphere proxy on the active connection and register it
    # in the sources group.
    sphere = Sphere(ThetaResolution=16, PhiResolution=32)

    # Apply a shrink filter
    shrink = Shrink(sphere)

    # Turn the visibility of the shrink object on.
    Show(shrink)

    # Render the scene
    Render()

Scripts generated by ParaView's Python Tracing, Python state saving, and
Catalyst saving use functions in this module.
"""

import paraview
from paraview import servermanager

from paraview.servermanager import (
    OutputPort,
)

from paraview.simple.version import (
    GetOpenGLInformation,
    GetParaViewSourceVersion,
    GetParaViewVersion,
)
from paraview.simple.session import (
    Connect,
    Disconnect,
    ResetSession,
    ReverseConnect,
    SetActiveConnection,
    GetActiveView,
    GetActiveSource,
    GetActiveCamera,
    SetActiveSource,
    SetActiveView,
    _add_functions,
    _DisableFirstRenderCameraReset,
)
from paraview.simple.proxy import (
    RenameProxy,
    RenameSource,
    RenameView,
    FindSource,
    FindView,
    GetActiveViewOrCreate,
    FindViewOrCreate,
    LocateView,
    GetSources,
    GetRepresentations,
    Delete,
    ResetProperty,
    UpdatePipeline,
)
from paraview.simple.plugin import (
    LoadXML,
    LoadPlugins,
    LoadPlugin,
    LoadDistributedPlugin,
    LoadCustomFilters,
)
from paraview.simple.io import (
    LoadState,
    SaveState,
    OpenDataFile,
    ReloadFiles,
    ExtendFileSeries,
    ReplaceReaderFileName,
    CreateWriter,
    SaveData,
    SaveScreenshot,
    SetNumberOfCallbackThreads,
    GetNumberOfCallbackThreads,
    SetNumberOfSMPThreads,
    GetNumberOfSMPThreads,
    WaitForScreenshot,
    SaveAnimation,
    WriteAnimationGeometry,
    FetchData,
)
from paraview.simple.view import (
    CreateView,
    CreateRenderView,
    CreateXYPlotView,
    CreateXYPointPlotView,
    CreateBarChartView,
    CreateComparativeRenderView,
    CreateComparativeXYPlotView,
    CreateComparativeBarChartView,
    CreateParallelCoordinatesChartView,
    Create2DRenderView,
    GetRenderView,
    GetRenderView,
    GetRenderViews,
    GetViews,
    SetViewProperties,
    ExportView,
    ImportView,
)
from paraview.simple.rendering import (
    Render,
    RenderAllViews,
    Interact,
    GetRepresentation,
    GetDisplayProperties,  # to deprectate
    Show,
    ShowAll,
    Hide,
    HideAll,
    SetRepresentationProperties,
    SetDisplayProperties,  # to deprecate
    ColorBy,
    ColorBlocksBy,
)

from paraview.simple.camera import (
    ResetCamera,
    ResetCameraToDirection,
)
from paraview.simple.layout import (
    RenameLayout,
    CreateLayout,
    RemoveLayout,
    GetLayouts,
    GetLayout,
    GetLayoutByName,
    GetViewsInLayout,
    AssignViewToLayout,
    RemoveViewsAndLayouts,
    EqualizeViewsHorizontally,
    EqualizeViewsVertically,
    EqualizeViewsBoth,
)
from paraview.simple.color import (
    LoadPalette,
    GenerateRGBPoints,
    GetColorTransferFunction,
    GetBlockColorTransferFunction,
    GetOpacityTransferFunction,
    GetTransferFunction2D,
    ImportPresets,
    ExportTransferFunction,
    CreateLookupTable,
    CreatePiecewiseFunction,
    AssignFieldToColorPreset,
    ListColorPresetNames,
)

from paraview.simple.link import (
    RemoveLink,
    AddProxyLink,
    AddViewLink,
    AddRenderViewLink,
    AddCameraLink,
    RemoveCameraLink,
    AddSelectionLink,
    RemoveSelectionLink,
)
from paraview.simple.animation import (
    GetTimeKeeper,
    GetAnimationScene,
    AnimateReader,
    GetRepresentationAnimationHelper,
    GetAnimationTrack,
    GetCameraTrack,
    GetTimeTrack,
)
from paraview.simple.light import (
    CreateLight,
    AddLight,
    RemoveLight,
    GetLight,
    GetViewForLight,
)
from paraview.simple.widget import (
    HideUnusedScalarBars,
    HideScalarBarIfNotNeeded,
    UpdateScalarBars,
    UpdateScalarBarsComponentTitle,
    GetScalarBar,
    ShowInteractiveWidgets,
    HideInteractiveWidgets,
)
from paraview.simple.selection import (
    SelectionProxy,
    CreateSelection,
    SelectSurfacePoints,
    SelectSurfaceCells,
    SelectSurfaceBlocks,
    SelectPointsThrough,
    SelectCellsThrough,
    SelectGlobalIDs,
    SelectPedigreeIDs,
    SelectIDs,
    SelectCompositeDataIDs,
    SelectHierarchicalDataIDs,
    SelectThresholds,
    SelectLocation,
    QuerySelect,
    SelectCells,
    SelectPoints,
    ClearSelection,
)
from paraview.simple.texture import (
    GetMaterialLibrary,
    CreateTexture,
    FindTextureOrCreate,
)
from paraview.simple.catalyst import (
    CreateExtractor,
    GetExtractors,
    FindExtractor,
    SaveExtractsUsingCatalystOptions,
    SaveExtracts,
    CreateSteerableParameters,
    UpdateSteerableParameters,
)
from paraview.simple.setting import (
    GetSettingsProxy,
    GetAllSettings,
)

from paraview.simple.deprecated import (
    MakeBlueToRedLT,
    SetProperties,
    GetProperty,
    GetRepresentationProperty,
    GetDisplayProperty,
    GetViewProperties,
    GetViewProperty,
    AssignLookupTable,
    GetLookupTableNames,
    LoadLookupTable,
)

__all__ = [
    "_extend_simple",
    # -- servermanager
    "paraview",  # needed to do `from paraview.simple import *;paraview.simple._DisableFirstRenderCameraReset()`
    "servermanager",  # needed for legacy import usage
    "OutputPort",
    # -- version
    "GetOpenGLInformation",
    "GetParaViewSourceVersion",
    "GetParaViewVersion",
    # -- session
    "Connect",
    "Disconnect",
    "ResetSession",
    "ReverseConnect",
    "SetActiveConnection",
    "_DisableFirstRenderCameraReset",
    # -- active
    "GetActiveView",
    "GetActiveSource",
    "GetActiveCamera",
    "SetActiveSource",
    "SetActiveView",
    # -- proxy
    "RenameProxy",
    "RenameSource",
    "RenameView",
    "FindSource",
    "FindView",
    "GetActiveViewOrCreate",
    "FindViewOrCreate",
    "LocateView",
    "GetSources",
    "GetRepresentations",
    "Delete",
    "ResetProperty",
    "UpdatePipeline",
    # -- plugin
    "LoadXML",
    "LoadPlugins",
    "LoadPlugin",
    "LoadDistributedPlugin",
    "LoadCustomFilters",
    # -- io
    "LoadState",
    "SaveState",
    "OpenDataFile",
    "ReloadFiles",
    "ExtendFileSeries",
    "ReplaceReaderFileName",
    "CreateWriter",
    "SaveData",
    "SaveScreenshot",
    "SetNumberOfCallbackThreads",
    "GetNumberOfCallbackThreads",
    "SetNumberOfSMPThreads",
    "GetNumberOfSMPThreads",
    "WaitForScreenshot",
    "SaveAnimation",
    "WriteAnimationGeometry",
    "FetchData",
    # -- view
    "CreateView",
    "CreateRenderView",
    "CreateXYPlotView",
    "CreateXYPointPlotView",
    "CreateBarChartView",
    "CreateComparativeRenderView",
    "CreateComparativeXYPlotView",
    "CreateComparativeBarChartView",
    "CreateParallelCoordinatesChartView",
    "Create2DRenderView",
    "GetRenderView",
    "GetRenderView",
    "GetRenderViews",
    "GetViews",
    "SetViewProperties",
    "ExportView",
    "ImportView",
    # -- rendering
    "Render",
    "RenderAllViews",
    "Interact",
    "GetRepresentation",
    "GetDisplayProperties",
    "Show",
    "ShowAll",
    "Hide",
    "HideAll",
    "SetRepresentationProperties",
    "SetDisplayProperties",
    "ColorBy",
    "ColorBlocksBy",
    # -- camera
    "ResetCamera",
    "ResetCameraToDirection",
    # -- layout
    "RenameLayout",
    "CreateLayout",
    "RemoveLayout",
    "GetLayouts",
    "GetLayout",
    "GetLayoutByName",
    "GetViewsInLayout",
    "AssignViewToLayout",
    "RemoveViewsAndLayouts",
    "EqualizeViewsHorizontally",
    "EqualizeViewsVertically",
    "EqualizeViewsBoth",
    # -- color
    "LoadPalette",
    "GenerateRGBPoints",
    "GetColorTransferFunction",
    "GetBlockColorTransferFunction",
    "GetOpacityTransferFunction",
    "GetTransferFunction2D",
    "ImportPresets",
    "ExportTransferFunction",
    "CreateLookupTable",
    "CreatePiecewiseFunction",
    "AssignFieldToColorPreset",
    "ListColorPresetNames",
    # -- link
    "RemoveLink",
    "AddProxyLink",
    "AddViewLink",
    "AddRenderViewLink",
    "AddCameraLink",
    "RemoveCameraLink",
    "AddSelectionLink",
    "RemoveSelectionLink",
    # -- animation
    "GetTimeKeeper",
    "GetAnimationScene",
    "AnimateReader",
    "GetRepresentationAnimationHelper",
    "GetAnimationTrack",
    "GetCameraTrack",
    "GetTimeTrack",
    # -- light
    "CreateLight",
    "AddLight",
    "RemoveLight",
    "GetLight",
    "GetViewForLight",
    # -- widget
    "HideUnusedScalarBars",
    "HideScalarBarIfNotNeeded",
    "UpdateScalarBars",
    "UpdateScalarBarsComponentTitle",
    "GetScalarBar",
    "ShowInteractiveWidgets",
    "HideInteractiveWidgets",
    # -- selection
    "SelectionProxy",
    "CreateSelection",
    "SelectSurfacePoints",
    "SelectSurfaceCells",
    "SelectSurfaceBlocks",
    "SelectPointsThrough",
    "SelectCellsThrough",
    "SelectGlobalIDs",
    "SelectPedigreeIDs",
    "SelectIDs",
    "SelectCompositeDataIDs",
    "SelectHierarchicalDataIDs",
    "SelectThresholds",
    "SelectLocation",
    "QuerySelect",
    "SelectCells",
    "SelectPoints",
    "ClearSelection",
    # -- texture
    "GetMaterialLibrary",
    "CreateTexture",
    "FindTextureOrCreate",
    # -- catalyst
    "CreateExtractor",
    "GetExtractors",
    "FindExtractor",
    "SaveExtractsUsingCatalystOptions",
    "SaveExtracts",
    "CreateSteerableParameters",
    "UpdateSteerableParameters",
    # -- setting
    "GetSettingsProxy",
    "GetAllSettings",
    # -- compatibility
    # -- deprecated
    "MakeBlueToRedLT",
    "SetProperties",
    "GetProperty",
    "GetRepresentationProperty",
    "GetDisplayProperty",
    "GetViewProperties",
    "GetViewProperty",
    "AssignLookupTable",
    "GetLookupTableNames",
    "LoadLookupTable",
]

# ==============================================================================
# Namespace extension handling
# ==============================================================================


def _extend_simple(*namespaces):
    local_ns = globals()
    for name in _add_functions(local_ns):
        if name not in __all__:
            __all__.append(name)

    for ns in namespaces:
        if ns is not local_ns:
            for name in _add_functions(ns):
                if name not in __all__:
                    __all__.append(name)


# ==============================================================================
# Start the session and initialize the ServerManager
# ==============================================================================

if not paraview.options.satellite:
    if not servermanager.ActiveConnection:
        Connect()
    else:
        # needed for catalyst pvbatch mpi
        _extend_simple()
