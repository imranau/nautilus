/*
 * Automatically generated C config: don't edit
 * Nautilus version: 
 * Sun Apr  9 21:59:16 2017
 */
#define AUTOCONF_INCLUDED

/*
 * Platform/Arch Options
 */
#define NAUT_CONFIG_X86_64_HOST 1
#undef NAUT_CONFIG_XEON_PHI
#undef NAUT_CONFIG_HVM_HRT
#define NAUT_CONFIG_MAX_CPUS 256
#define NAUT_CONFIG_MAX_IOAPICS 16
#undef NAUT_CONFIG_PALACIOS

/*
 * Nautilus AeroKernel Build Config
 */
#define NAUT_CONFIG_USE_NAUT_BUILTINS 1
#define NAUT_CONFIG_CXX_SUPPORT 1
#define NAUT_CONFIG_TOOLCHAIN_ROOT ""

/*
 * Interface Options
 */
#define NAUT_CONFIG_THREAD_EXIT_KEYCODE 196

/*
 * Nautilus AeroKernel Configuration
 */
#define NAUT_CONFIG_MAX_THREADS 1024
#undef NAUT_CONFIG_USE_TICKETLOCKS
#undef NAUT_CONFIG_VIRTUAL_CONSOLE_SERIAL_MIRROR

/*
 * Scheduler Options
 */
#define NAUT_CONFIG_UTILIZATION_LIMIT 99
#define NAUT_CONFIG_SPORADIC_RESERVATION 10
#define NAUT_CONFIG_APERIODIC_RESERVATION 10
#define NAUT_CONFIG_HZ 10
#undef NAUT_CONFIG_AUTO_REAP
#undef NAUT_CONFIG_WORK_STEALING
#undef NAUT_CONFIG_INTERRUPT_THREAD
#undef NAUT_CONFIG_APERIODIC_DYNAMIC_QUANTUM
#undef NAUT_CONFIG_APERIODIC_DYNAMIC_LIFETIME
#undef NAUT_CONFIG_APERIODIC_LOTTERY
#define NAUT_CONFIG_APERIODIC_ROUND_ROBIN 1
#undef NAUT_CONFIG_REAL_MODE_INTERFACE

/*
 * AeroKernel Performance Optimizations
 */
#define NAUT_CONFIG_FPU_SAVE 1
#undef NAUT_CONFIG_KICK_SCHEDULE
#undef NAUT_CONFIG_HALT_WHILE_IDLE
#undef NAUT_CONFIG_THREAD_OPTIMIZE
#undef NAUT_CONFIG_USE_IDLE_THREADS

/*
 * Debugging
 */
#undef NAUT_CONFIG_DEBUG_INFO
#undef NAUT_CONFIG_DEBUG_PRINTS
#undef NAUT_CONFIG_ENABLE_ASSERTS
#undef NAUT_CONFIG_PROFILE
#undef NAUT_CONFIG_SILENCE_UNDEF_ERR
#undef NAUT_CONFIG_ENABLE_STACK_CHECK
#undef NAUT_CONFIG_DEBUG_VIRTUAL_CONSOLE
#undef NAUT_CONFIG_DEBUG_DEV

/*
 * Parallel Runtime Integration
 */
#undef NAUT_CONFIG_LEGION_RT
#undef NAUT_CONFIG_NDPC_RT
#undef NAUT_CONFIG_NESL_RT
#define NAUT_CONFIG_NO_RT 1

/*
 * Device options
 */
#undef NAUT_CONFIG_SERIAL_REDIRECT
#define NAUT_CONFIG_SERIAL_PORT 1
#undef NAUT_CONFIG_APIC_FORCE_XAPIC_MODE
#undef NAUT_CONFIG_APIC_TIMER_CALIBRATE_INDEPENDENTLY
#undef NAUT_CONFIG_HPET
#undef NAUT_CONFIG_VIRTIO_PCI
#undef NAUT_CONFIG_RAMDISK
#undef NAUT_CONFIG_ATA

/*
 * Filesystems
 */
#undef NAUT_CONFIG_EXT2_FILESYSTEM_DRIVER
