# This script is called as part of the platformio build sequence for
# the stm32 controller firmware.
#
# It's purpose is to add some additional options which can't be
# directly added to the platformio.ini line for some reason.
Import("env")

# Threadsafe-statics ends up calling malloc.  Anyway we don't need this, as we
# don't have proper threads.
env.Append(CXXFLAGS=["-fno-threadsafe-statics"])

# These link flags tell the linker that the processor we're using has
# hardware floating point, so the software floating point libraries aren't
# needed.
env.Append(LINKFLAGS=["-mfpu=fpv4-sp-d16", "-mfloat-abi=hard"])
