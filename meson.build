project('ai-reel', 'c',
  version: '1.0.0',
  default_options : [
    'c_std=c17',
    'warning_level=1',
  ])

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)

dep_glib = dependency('glib-2.0', version: '>= 2.74')
dep_gio = dependency('gio-2.0', version: '>= 2.74')
dep_json = dependency('json-glib-1.0', version: '>= 1.6')
dep_sqlite3 = dependency('sqlite3', version: '>= 3.30')
dep_secret = dependency('libsecret-1', version: '>= 0.20')
dep_curl = dependency('libcurl', version: '>= 7.68')
dep_gtk = dependency('gtk4', version: '>= 4.10')
dep_gst = dependency('gstreamer-1.0', version: '>= 1.20')
dep_gst_video = dependency('gstreamer-video-1.0', version: '>= 1.20')

core_deps = [
  dep_glib,
  dep_gio,
  dep_json,
  dep_sqlite3,
  dep_secret,
  dep_curl,
  dep_gtk,
  dep_gst,
  dep_gst_video,
  m_dep
]

inc = include_directories('include', 'src', 'src/storage', 'src/providers', 'src/media', 'src/generation', 'src/ui')

# Core Library & Components
core_src = [
  'src/utils/types.c',
  'src/utils/json_utils.c',
  'src/app/core.c',
  'src/app/context.c',
  'src/app/job_manager.c',
  'src/network/http_client.c',
  'src/security/credential_manager.c',
  'src/storage/database.c',
  'src/storage/project_store.c',
  'src/storage/provider_store.c',
  'src/storage/job_store.c',
  'src/storage/media_store.c',
  'src/storage/reel_json.c',
  'src/providers/provider_common.c',
  'src/providers/gemini.c',
  'src/providers/openrouter.c',
  'src/providers/openai_compatible.c',
  'src/providers/provider_registry.c',
  'src/generation/planner.c',
  'src/generation/pipeline.c',
  'src/generation/narration.c',
  'src/generation/assets.c',
  'src/media/ffmpeg.c',
  'src/media/captions.c',
  'src/media/music.c',
  'src/media/asset_search.c',
  'src/media/renderer.c',
]

ui_src = [
  'src/ui/ui_common.c',
  'src/ui/dashboard.c',
  'src/ui/reel_creator.c',
  'src/ui/project_library.c',
  'src/ui/api_manager.c',
  'src/ui/settings.c',
  'src/ui/workspace.c',
  'src/app/application.c',
]

# Main Application Executable
executable('ai-reel',
  ['src/main.c'] + core_src + ui_src,
  include_directories: inc,
  dependencies: core_deps,
  install: true)

# Tests
t_storage = executable('test_storage',
  ['tests/test_storage.c'] + core_src,
  include_directories: inc,
  dependencies: core_deps)
test('storage_test', t_storage)

t_media = executable('test_media',
  ['tests/test_media.c'] + core_src,
  include_directories: inc,
  dependencies: core_deps)
test('media_test', t_media)

t_providers = executable('test_providers',
  ['tests/test_providers.c'] + core_src,
  include_directories: inc,
  dependencies: core_deps)
test('providers_test', t_providers)

t_generation = executable('test_generation',
  ['tests/test_generation.c'] + core_src,
  include_directories: inc,
  dependencies: core_deps)
test('generation_test', t_generation)

# Desktop & Data Installation
install_data('data/ai-reel.desktop',
  install_dir: get_option('datadir') / 'applications')

install_data('data/org.aireelstudio.AiReelStudio.metainfo.xml',
  install_dir: get_option('datadir') / 'metainfo')

install_data('data/icons/ai-reel.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps')

install_data([
  'docs/ARCHITECTURE.md',
  'docs/MIGRATION.md',
  'docs/API_PROVIDER_SYSTEM.md',
  'docs/MEDIA_PIPELINE.md',
  'docs/SECURITY.md',
  'docs/DATABASE_SCHEMA.md',
  'docs/BUILD.md',
  'README.md',
], install_dir: get_option('datadir') / 'ai-reel-studio')
