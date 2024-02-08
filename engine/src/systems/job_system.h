#pragma once

#include "defines.h"

/** @brief A function pointer definition for jobs. */
typedef b8 (*pfn_job_start)(void*, void*);

/** @brief A function pointer definition for completion of a job. */
typedef void (*pfn_job_on_complete)(void*);

struct frame_data;

/** @brief Describes a type of job */
typedef enum job_type {
    /**
     * @brief A general job that does not have any specific thread requirements.
     * This means it matters little which job thread this job runs on.
     */
    JOB_TYPE_GENERAL = 0x02,

    /**
     * @brief A resource loading job. Resources should always load on the same thread
     * to avoid potential disk thrashing.
     */
    JOB_TYPE_RESOURCE_LOAD = 0x04,

    /**
     * @brief Jobs using GPU resources should be bound to a thread using this job type. Multithreaded
     * renderers will use a specific job thread, and this type of job will run on that thread.
     * For single-threaded renderers, this will be on the main thread.
     */
    JOB_TYPE_GPU_RESOURCE = 0x08,
} job_type;

/**
 * @brief Determines which job queue a job uses. The high-priority queue is always
 * exhausted first before processing the normal-priority queue, which must also
 * be exhausted before processing the low-priority queue.
 */
typedef enum job_priority {
    /** @brief The lowest-priority job, used for things that can wait to be done if need be, such as log flushing. */
    JOB_PRIORITY_LOW,
    /** @brief A normal-priority job. Should be used for medium-priority tasks such as loading assets. */
    JOB_PRIORITY_NORMAL,
    /** @brief The highest-priority job. Should be used sparingly, and only for time-critical operations.*/
    JOB_PRIORITY_HIGH
} job_priority;

/**
 * @brief Describes a job to be run.
 */
typedef struct job_info {
    /** @brief The type of job. Used to determine which thread the job executes on. */
    job_type type;

    /** @brief The uniquie identifier of this job. */
    u16 id;

    /** @brief The priority of this job. Higher priority jobs obviously run sooner. */
    job_priority priority;

    /** @brief A function pointer to be invoked when the job starts. Required. */
    pfn_job_start entry_point;

    /** @brief A function pointer to be invoked when the job successfully completes. Optional. */
    pfn_job_on_complete on_success;

    /** @brief A function pointer to be invoked when the job successfully fails. Optional. */
    pfn_job_on_complete on_fail;

    /** @brief Data to be passed to the entry point upon execution. */
    void* param_data;

    /** @brief The size of the data passed to the job. */
    u32 param_data_size;

    /** @brief Data to be passed to the success/fail function upon execution, if exists. */
    void* result_data;

    /** @brief The size of the data passed to the success/fail function. */
    u32 result_data_size;

    /** @brief A count of job identifiers that must be complete before this job starts. */
    u8 dependency_count;

    /** @brief An array of job identifiers that must be complete before this job starts. */
    u16* dependency_ids;
} job_info;

typedef struct job_system_config {
    /**
     * @param max_job_thread_count The maximum number of job threads to be spun up.
     * Should be no more than the number of cores on the CPU, minus one to account for the main thread.
     */
    u8 max_job_thread_count;
    /** @param type_masks A collection of type masks for each job thread. Must match max_job_thread_count. */
    u32* type_masks;
} job_system_config;

/**
 * @brief Initializes the job system. Call once to retrieve job_system_memory_requirement, passing 0 to state. Then
 * call a second time with allocated state memory block.
 * @param job_system_memory_requirement A pointer to hold the memory required for the job system state in bytes.
 * @param state A block of memory to hold the state of the job system.
 * @param config A pointer to the configuration (job_system_config) of this system.
 * @returns True if the job system started up successfully; otherwise false.
 */
b8 job_system_initialize(u64* job_system_memory_requirement, void* state, void* config);

/**
 * @brief Shuts the job system down.
 */
void job_system_shutdown(void* state);

/**
 * @brief Updates the job system. Should happen once an update cycle.
 */
b8 job_system_update(void* state, struct frame_data* p_frame_data);

/**
 * @brief Submits the provided job to be queued for execution.
 * @param info The description of the job to be executed.
 */
KAPI void job_system_submit(job_info info);

/**
 * @brief Creates a new job with default type (Generic) and priority (Normal).
 * @param entry_point A pointer to a function to be invoked when the job starts. Required.
 * @param on_success A pointer to a function to be invoked when the job completes successfully. Optional.
 * @param on_fail A pointer to a function to be invoked when the job fails. Optional.
 * @param param_data Data to be passed to the entry point upon execution.
 * @param param_data_size The data to be passed on to entry_point callback. Pass 0 if not used.
 * @param result_data_size The size of result data to be passed on to success callback. Pass 0 if not used.
 * @returns The newly created job information to be submitted for execution.
 */
KAPI job_info job_create(pfn_job_start entry_point, pfn_job_on_complete on_success, pfn_job_on_complete on_fail, void* param_data, u32 param_data_size, u32 result_data_size);

/**
 * @brief Creates a new job with default priority (Normal).
 * @param entry_point A pointer to a function to be invoked when the job starts. Required.
 * @param on_success A pointer to a function to be invoked when the job completes successfully. Optional.
 * @param on_fail A pointer to a function to be invoked when the job fails. Optional.
 * @param param_data Data to be passed to the entry point upon execution.
 * @param param_data_size The data to be passed on to entry_point callback. Pass 0 if not used.
 * @param result_data_size The size of result data to be passed on to success callback. Pass 0 if not used.
 * @param type The type of job. Used to determine which thread the job executes on.
 * @returns The newly created job information to be submitted for execution.
 */
KAPI job_info job_create_type(pfn_job_start entry_point, pfn_job_on_complete on_success, pfn_job_on_complete on_fail, void* param_data, u32 param_data_size, u32 result_data_size, job_type type);

/**
 * @brief Creates a new job with the provided priority.
 * @param entry_point A pointer to a function to be invoked when the job starts. Required.
 * @param on_success A pointer to a function to be invoked when the job completes successfully. Optional.
 * @param on_fail A pointer to a function to be invoked when the job fails. Optional.
 * @param param_data Data to be passed to the entry point upon execution.
 * @param param_data_size The data to be passed on to entry_point callback. Pass 0 if not used.
 * @param result_data_size The size of result data to be passed on to success callback. Pass 0 if not used.
 * @param type The type of job. Used to determine which thread the job executes on.
 * @param priority The priority of this job. Higher priority jobs obviously run sooner.
 * @returns The newly created job information to be submitted for execution.
 */
KAPI job_info job_create_priority(pfn_job_start entry_point, pfn_job_on_complete on_success, pfn_job_on_complete on_fail, void* param_data, u32 param_data_size, u32 result_data_size, job_type type, job_priority priority);

/**
 * @brief Creates a new job with the provided type, priority, and dependencies.
 * @param entry_point A pointer to a function to be invoked when the job starts. Required.
 * @param on_success A pointer to a function to be invoked when the job completes successfully. Optional.
 * @param on_fail A pointer to a function to be invoked when the job fails. Optional.
 * @param param_data Data to be passed to the entry point upon execution.
 * @param param_data_size The data to be passed on to entry_point callback. Pass 0 if not used.
 * @param result_data_size The size of result data to be passed on to success callback. Pass 0 if not used.
 * @param type The type of job. Used to determine which thread the job executes on.
 * @param priority The priority of this job. Higher priority jobs obviously run sooner.
 * @param dependency_count The number of job identifiers which must be complete before this job runs.
 * @param dependencies An array of job identifiers which must be complete before this job runs.
 * @returns The newly created job information to be submitted for execution.
 */
KAPI job_info job_create_with_dependencies(
    pfn_job_start entry_point,
    pfn_job_on_complete on_success,
    pfn_job_on_complete on_fail,
    void* param_data,
    u32 param_data_size,
    u32 result_data_size,
    job_type type,
    job_priority priority,
    u8 dependency_count,
    u16* dependencies);

/**
 * @brief Returns whether or not the job with the given identifier has completed.
 */
KAPI b8 job_system_query_job_complete(u16 job_id);
