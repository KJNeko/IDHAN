# Table of Contents

- [Job types](#job-types)
    - [Cluster Scan](#cluster-scan)

# Job endpoints

- `GET /jobs/status` — list status of all jobs
- `GET /jobs/{job_id}/status` — status of a specific job
- `POST /jobs/metadata/rescan` — trigger a metadata rescan job

# Job types

## Cluster Scan

Cluster scan jobs are started via the cluster API: `POST /clusters/{cluster_id}/scan`

The cluster scan job scans a given cluster for files that are missing or corrupted.

# Job Statuses

- Pending: The job has not been started but the information is stored in the table
- Started: The job has started execution, If a job supports being canceled then it can return to `PENDING` if it's
  paused.
- Completed: The job has finished execution
- Failed: The job has failed to execute
- Await Dependency: The job is waiting on another job to complete before starting.

# Internals

When a job is created, It will begin with preparing the context and send it to the DB. From there a worker will take
jobs
from the `jobs` table and execute them given a few rules.

A job can have a dependency on another job being completed.

- A job is requested
- The job context is created with the information required
- `prepare()` is called, This will create any dependency jobs and prepare the context further.

# Job Dependencies

Job dependencies are used to break up large jobs into smaller jobs that can be completed in parallel on multiple
threads.
One example of this is a cluster scan job. The cluster scan job will start scan jobs on every file in the cluster.
Which also have dependencies such as mime scanning and mime parsing (metadata)
The cluster scan job will not report completed until all other jobs have completed.
