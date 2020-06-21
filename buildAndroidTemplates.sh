export ANDROID_HOME=/home/ai/android
export ANDROID_NDK_ROOT=/home/ai/android/ndk/18.1.5063045

scons -j8 platform=android target=release_debug android_arch=armv7
scons -j8 platform=android target=release_debug android_arch=arm64v8
docker-compose run --rm godot

#scons platform=android target=release android_arch=armv8
#scons platform=android target=release android_arch=arm64v8
#docker-compose run --rm godot

