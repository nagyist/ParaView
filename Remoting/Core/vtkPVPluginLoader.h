// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause
/**
 * @class   vtkPVPluginLoader
 * @brief   Used to load ParaView plugins.
 *
 * vtkPVPluginLoader can be used to load plugins for ParaView. vtkPVPluginLoader
 * loads the plugin on the local process.
 *
 * vtkPVPluginLoader logs plugin related messages using at
 * `PARAVIEW_LOG_PLUGIN_VERBOSITY` level. See `vtkPVLogger::SetPluginVerbosity`
 * for information on using environment variables to override or elevate the
 * verbosity level.
 *
 * This class only needed when loading plugins from shared libraries
 * dynamically. For statically importing plugins, one directly uses
 * PV_PLUGIN_IMPORT() macro defined in vtkPVPlugin.h.
 */

#ifndef vtkPVPluginLoader_h
#define vtkPVPluginLoader_h

#include "vtkObject.h"
#include "vtkParaViewDeprecation.h" // for deprecation macro
#include "vtkRemotingCoreModule.h"  //needed for exports

#include <functional> // for std::function

class vtkPVPlugin;

class VTKREMOTINGCORE_EXPORT vtkPVPluginLoader : public vtkObject
{
public:
  static vtkPVPluginLoader* New();
  vtkTypeMacro(vtkPVPluginLoader, vtkObject);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /**
   * Tries to the load the plugin by name using tracked plugins.
   */
  bool LoadPluginByName(const char* name, bool acceptDelayed = true);

  /**
   * Tries to the load the plugin given the path to the plugin file.
   */
  bool LoadPlugin(const char* filename) { return this->LoadPluginInternal(filename, false); }
  bool LoadPluginSilently(const char* filename) { return this->LoadPluginInternal(filename, true); }

  /**
   * Load a delayed load plugin with provided name, XMLs and an actual plugin filename
   * The actual loading will take place the first time a VTK object for a XML proxy needs to be
   * created.
   */
  PARAVIEW_DEPRECATED_IN_6_0_0("Use the overload with versions and description instead")
  bool LoadDelayedLoadPlugin(
    const std::string& name, const std::vector<std::string>& xmls, const std::string& filename);

  /**
   * Load a delayed load plugin with provided name, XMLs, an actual plugin filename, version and
   * description The actual loading will take place the first time a VTK object for a XML proxy
   * needs to be created.
   */
  bool LoadDelayedLoadPlugin(const std::string& name, const std::vector<std::string>& xmls,
    const std::string& filename, const std::string& version, const std::string& description);

  ///@{
  /**
   * Simply forwards the call to
   * vtkPVPluginTracker to load configuration xml.
   */
  void LoadPluginConfigurationXMLFromString(const char* xmlcontents);
  void LoadPluginConfigurationXML(const char* configurationFile);
  ///@}

  /**
   * Loads all plugins under the directories mentioned in the SearchPaths.
   */
  void LoadPluginsFromPluginSearchPath();

  /**
   * Use PV_PLUGIN_CONFILE_FILE xml file to load specified plugins
   * It can contain path to multiples xml pluginc config files
   * sperated by env separator.
   * It allow user to fine pick which plugins to load, instead of using PV_PLUGIN_PATH
   * the format a xml plugin file should be the following :
   *
   * \code{.xml}
   * <?xml version="1.0"?>
   * <Plugins>
   * <Plugin name="MyPlugin" filename="absolute/path/to/libMyPlugin.so"/>
   * ...
   * </Plugins>
   * \endcode
   */
  void LoadPluginsFromPluginConfigFile();

  /**
   * Loads all plugin libraries at a path.
   */
  void LoadPluginsFromPath(const char* path);

  ///@{
  /**
   * Returns the full filename for the plugin attempted to load most recently
   * using LoadPlugin().
   */
  vtkGetStringMacro(FileName);
  ///@}

  ///@{
  /**
   * Get the plugin name. This returns a valid name only after the plugin has
   * been loaded.
   */
  vtkGetStringMacro(PluginName);
  ///@}

  ///@{
  /**
   * Get the plugin version string. This returns a valid version string only
   * after the plugin has been loaded.
   */
  vtkGetStringMacro(PluginVersion);
  ///@}

  ///@{
  /**
   * Get the error string if the plugin failed to load. Returns nullptr if the
   * plugin was loaded successfully.
   */
  vtkGetStringMacro(ErrorString);
  ///@}

  ///@{
  /**
   * Get a string of standard search paths (path1;path2;path3)
   * search paths are based on PV_PLUGIN_PATH,
   * plugin dir relative to executable.
   */
  vtkGetStringMacro(SearchPaths);
  ///@}

  ///@{
  /**
   * Returns the status of most recent LoadPlugin call.
   */
  vtkGetMacro(Loaded, bool);
  ///@}

  ///@{
  /**
   */
  using PluginLoaderCallback = std::function<bool(const char*)>;
  static int RegisterLoadPluginCallback(PluginLoaderCallback callback);
  static void UnregisterLoadPluginCallback(int id);
  ///@}

  /**
   * Internal method used in pqParaViewPlugin.cxx.in to tell the
   * vtkPVPluginLoader that a library was unloaded so it doesn't try to unload
   * it again.
   */
  static void PluginLibraryUnloaded(const char* pluginname);

protected:
  vtkPVPluginLoader();
  ~vtkPVPluginLoader() override;

  bool LoadPluginInternal(const char* filename, bool no_errors);

  /**
   * Called by LoadPluginInternal() to do the final steps in loading of a
   * plugin.
   */
  bool LoadPluginInternal(vtkPVPlugin* plugin);

  /**
   * Return true if a plugin pointing provided filename is already loaded, return false otherwise.
   * Delayed load plugin are considered not loaded until they actually are as standard plugins.
   */
  bool IsLoaded(const char* filename, bool acceptDelayed = false);

  vtkSetStringMacro(ErrorString);
  vtkSetStringMacro(PluginName);
  vtkSetStringMacro(PluginVersion);
  vtkSetStringMacro(FileName);
  vtkSetStringMacro(SearchPaths);

  char* ErrorString;
  char* PluginName;
  char* PluginVersion;
  char* FileName;
  char* SearchPaths;
  bool Loaded;

private:
  vtkPVPluginLoader(const vtkPVPluginLoader&) = delete;
  void operator=(const vtkPVPluginLoader&) = delete;
  static bool CallPluginLoaderCallbacks(const char* nameOrFile);
};

// Implementation of Schwartz counter idiom to ensure that the plugin library
// unloading doesn't happen before the ParaView application is finalized.
static class VTKREMOTINGCORE_EXPORT vtkPVPluginLoaderCleanerInitializer
{
public:
  vtkPVPluginLoaderCleanerInitializer();
  ~vtkPVPluginLoaderCleanerInitializer();
} vtkPVPluginLoaderCleanerInitializerInstance; // object here in header.

#endif
