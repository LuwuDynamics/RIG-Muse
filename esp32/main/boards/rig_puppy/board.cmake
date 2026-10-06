set(RIG_BOARD_INCLUDE "boards/rig_puppy")
list(APPEND GADGET_SRCS
    "boards/rig_puppy/puppy_robot.c"
    "boards/rig_puppy/puppy_actions.c"
    "boards/rig_puppy/puppy_gait.c"
    "boards/rig_puppy/puppy_native.c"
    "boards/rig_puppy/rig_motion.c"
    "boards/rig_puppy/rig_media.c"
    "boards/rig_puppy/puppy_camera.c"
    "boards/rig_puppy/puppy_laser.c"
    "boards/rig_puppy/puppy_imu.c"
    "boards/rig_puppy/puppy_imu_model.c"
    "boards/rig_puppy/puppy_voice.c")
