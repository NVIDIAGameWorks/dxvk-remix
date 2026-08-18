USD Schema Plugins
==================

This directory contains codeless USD schema plugins. They use the no-Python
OpenUSD package and do not run `usdGenSchema` during development or builds.

Directory Layout
----------------

```
usd-plugins/
└── <PluginName>/
    ├── <schema API>.cpp
    ├── <schema API>.h
    ├── meson.build
    └── resources/
        ├── generatedSchema.usda
        ├── meson.build
        └── plugInfo.json
```

Despite its conventional name, `generatedSchema.usda` is the checked-in,
canonical schema source. Edit it directly. The build only copies the schema,
plugin metadata, and minimal TfType registration library into the runtime.

How to Add a New Plugin
-----------------------

1. Copy an existing plugin directory and rename its API, library, and metadata.
2. Define the API and properties directly in `resources/generatedSchema.usda`.
3. Update `resources/plugInfo.json` to match the schema identifier and library.
4. Add the plugin directory to `schema_plugins` in `usd-plugins/meson.build`.
5. Add a registry unit test that loads the build-tree plugin and compares its
   exact property names with the C++ code that consumes them.

No generated C++ schema classes are required. The small plugin library only
registers the API TfType needed by `HasAPI()`; property definitions come from
the codeless schema.
