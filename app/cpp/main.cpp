#include <android_native_app_glue.h>

#include <shaders.hpp>

#include "platform_data.hpp"
#include "platform.hpp"

#include "logger.hpp"

#include "openxr_program.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>
#include <android/native_activity.h>
#include <unistd.h>

struct AndroidAppState {
  bool resumed = false;
};

static void AppHandleCmd(struct android_app *app, int32_t cmd) {
  auto *app_state = reinterpret_cast<AndroidAppState *>(app->userData);
  switch (cmd) {
    case APP_CMD_START: {
      utils::logger::Log(utils::logger::Level::INFO, "APP_CMD_START onStart()");
      break;
    }
    case APP_CMD_RESUME: {
      utils::logger::Log(utils::logger::Level::INFO, "APP_CMD_RESUME onResume()");
      app_state->resumed = true;
      break;
    }
    case APP_CMD_PAUSE: {
      utils::logger::Log(utils::logger::Level::INFO, "APP_CMD_PAUSE onPause()");
      app_state->resumed = false;
      break;
    }
    case APP_CMD_STOP: {
      utils::logger::Log(utils::logger::Level::INFO, "APP_CMD_STOP onStop()");
      break;
    }
    case APP_CMD_DESTROY: {
      utils::logger::Log(utils::logger::Level::INFO, "APP_CMD_DESTROY onDestroy()");
      break;
    }
    case APP_CMD_INIT_WINDOW: {
      utils::logger::Log(utils::logger::Level::INFO, "APP_CMD_INIT_WINDOW surfaceCreated()");
      break;
    }
    case APP_CMD_TERM_WINDOW: {
      utils::logger::Log(utils::logger::Level::INFO, "APP_CMD_TERM_WINDOW surfaceDestroyed()");
      break;
    }
  }
}

extern "C" {
JNIEXPORT void JNICALL Java_com_app_paphos_flit_MyActivity_initialize(JNIEnv* env, jobject thiz, jobject javaAssetManager) {
  AAssetManager* asset_manager = AAssetManager_fromJava(env, javaAssetManager);
  AAsset* asset = AAssetManager_open(asset_manager, "angel_writer.png", AASSET_MODE_BUFFER);
  const void* data = AAsset_getBuffer(asset);
  off_t length = AAsset_getLength(asset);
  AAsset_close(asset);
}
}


void android_main(struct android_app *app) {
  try {

    LoadShaders();
    JNIEnv *env;
    app->activity->vm->AttachCurrentThread(&env, nullptr);

    AndroidAppState app_state = {};

    app->userData = &app_state;
    app->onAppCmd = AppHandleCmd;

    std::shared_ptr<PlatformData> data = std::make_shared<PlatformData>();
    data->application_vm = app->activity->vm;
    data->application_activity = app->activity->clazz;

    // ---

    // https://en.wikibooks.org/wiki/OpenGL_Programming/Android_GLUT_Wrapper#Accessing_assets
    jclass activity_class = env->GetObjectClass(app->activity->clazz);
    // Get path to cache dir (/data/data/org.wikibooks.OpenGL/cache)
    jmethodID get_cache_dir = env->GetMethodID(activity_class, "getCacheDir", "()Ljava/io/File;");
    jobject file = env->CallObjectMethod(app->activity->clazz, get_cache_dir);
    jclass file_class = env->FindClass("java/io/File");
    jmethodID get_absolute_path = env->GetMethodID(file_class, "getAbsolutePath", "()Ljava/lang/String;");
    jstring jpath = (jstring)env->CallObjectMethod(file, get_absolute_path);
    const char* app_dir = env->GetStringUTFChars(jpath, NULL);

    // chdir in the application cache directory
    chdir(app_dir);
    utils::logger::Log(utils::logger::Level::DEBUG, fmt::format("app dir is {}", app_dir));
    env->ReleaseStringUTFChars(jpath, app_dir);

    AAssetManager* asset_manager = app->activity->assetManager;

    AAssetDir* assetDir = AAssetManager_openDir(asset_manager, "");
    const char* filename = (const char*)NULL;

    while ((filename = AAssetDir_getNextFileName(assetDir)) != NULL) {
      utils::logger::Log(utils::logger::Level::DEBUG, fmt::format("copy filename {}\n", filename));
      AAsset* asset = AAssetManager_open(asset_manager, filename, AASSET_MODE_STREAMING);
      char buf[BUFSIZ];
      int nb_read = 0;
      FILE* out = fopen(filename, "w");
      while ((nb_read = AAsset_read(asset, buf, BUFSIZ)) > 0)
        fwrite(buf, nb_read, 1, out);
      fclose(out);
      AAsset_close(asset);
    }
    AAssetDir_close(assetDir);

    AAsset* asset = AAssetManager_open(asset_manager, "angel_writer.png", AASSET_MODE_BUFFER);
    off_t length = AAsset_getLength(asset);

    // Read the image into a buffer
    unsigned char* buffer = new unsigned char[length];
    AAsset_read(asset, buffer, length);

    // Load image and render it into world

    // Logcat debug
    utils::logger::Log(utils::logger::Level::DEBUG, "TestLog!\n");

//    DIR *dp;
//    struct dirent *ep;
//
//    utils::logger::Log(utils::logger::Level::DEBUG, "attempt read directory");
//    dp = opendir (".");
//    if (dp != NULL)
//    {
//      utils::logger::Log(utils::logger::Level::DEBUG, "opened directory!");
//      while (ep = readdir (dp))
//        utils::logger::Log(utils::logger::Level::DEBUG, fmt::format("file in dir is {}", ep->d_name));
//      (void) closedir (dp);
//    }

    int width, height, channels;
    unsigned char* image = stbi_load_from_memory(buffer, length, &width, &height, &channels, STBI_default);
    utils::logger::Log(utils::logger::Level::DEBUG, fmt::format("image width is {}", width));

    // ---

    std::shared_ptr<OpenXrProgram> program = CreateOpenXrProgram(CreatePlatform(data));

    program->CreateInstance();
    program->InitializeSystem();
    program->InitializeSession();
    program->CreateSwapchains();
    while (app->destroyRequested == 0) {
      for (;;) {
        int events;
        struct android_poll_source *source;
        const int kTimeoutMilliseconds =
            (!app_state.resumed && !program->IsSessionRunning() &&
                app->destroyRequested == 0) ? -1 : 0;
        if (ALooper_pollAll(kTimeoutMilliseconds, nullptr, &events, (void **) &source) < 0) {
          break;
        }
        if (source != nullptr) {
          source->process(app, source);
        }
      }

      program->PollEvents();
      if (!program->IsSessionRunning()) {
        continue;
      }

      program->PollActions();
      program->RenderFrame();
    }

    app->activity->vm->DetachCurrentThread();
  } catch (const std::exception &ex) {
    utils::logger::Log(utils::logger::Level::FATAL, ex.what());
  } catch (...) {
    utils::logger::Log(utils::logger::Level::FATAL, "Unknown Error");
  }
}
