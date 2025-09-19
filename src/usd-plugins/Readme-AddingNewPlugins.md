
USD Schema Plugins
==================

This directory holds *schema-only* USD plugins (e.g. **RemixParticleSystem**) that are built as standalone shared libraries and discovered by USD at runtime.

Directory Layout
----------------

```
usd-plugins/
└── <PluginName>/
    ├── schema.usda         ← your API schema definition
    ├── meson.build         ← per-plugin build instructions

```

At build time, Meson will:

1.  Run **usdGenSchema** on `schema.usda`

2.  Emit C++/header files into the build tree

3.  Compile them into\
    `src/usd-plugins/<PluginName>/<PluginName>.(dll)`

Plugins generated in this way will automatically be linked and loaded into the runtime automatically

* * * * *

How to Add a New Plugin
-----------------------

1.  **Copy an Existing Plugin**

    ```
    cp -r plugins/RemixParticleSystem plugins/MyNewPlugin

    ```

2.  **Edit Your Schema**

    -   Open `plugins/MyNewPlugin/schema.usda`

    -   Rename the API class to match your plugin, e.g.

        ```
        class "MyNewPluginAPI" (
          customData = {
            string libraryName = "MyNewPlugin"
            string libraryPath = "pxr/usd/MyNewPlugin"
          }
          ...
        )
        ```

3.  **Add Class Names**\
    In `usd-plugins/MyNewPlugin/meson.build`, locate the array of classnames:
       
    ```
    plugin_classes = [
      'prim',
    ]
    ```
    
    - Enumerate any class names added in the USD schema (these will each generate source files we need to account for at build time)
    
4.  **Update Meson's Plugin List**\
    In `usd-plugins/meson.build` (or your root `meson.build`), locate the array of plugin names:

    ```
    schema_plugins = [
      'RemixParticleSystem',
      # add your new plugin here:
      'MyNewPlugin',
    ]
    ```
    - Add your new plugin name here!  This should be correlated with the subdirectory name and plugin name in the schema.