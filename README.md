# Operating System

## MLFQ and Stride scheduling

Designed and implemented a new scheduler that combines Multilevel Feedback Queue (MLFQ) and Stride Scheduling to enhance the default Xv6 scheduler.
Unfortunately, the design document and analysis document were lost due to a data failure on the school's GitLab server. They will be rewritten soon.

## Light-weight Process

Designed a Light-weight Process (LWP) model, a simplified version of Pthreads, and integrated it with Xv6 system calls and the MLFQ & Stride Scheduler.

### [Design](https://github.com/vinnyshin/OperatingSystem/blob/master/project02/project2%20milestone%201.md)
### [Analysis](https://github.com/vinnyshin/OperatingSystem/blob/master/project02/project2%20milestone%202.md)

## File System

### [sync](https://github.com/vinnyshin/OperatingSystem/blob/master/project03/project3%20milestone%201.md)
Modified the write system call to always write data into the buffer cache. Additionally, implemented a sync system call that flushes all cached data to disk to ensure data consistency.

### [multiple indirect blocks](https://github.com/vinnyshin/OperatingSystem/blob/master/project03/project3%20milestone%202.md)
Extended the file system's maximum file size by implementing triple indirect blocks, allowing support for significantly larger files.

### [pread & pwrite](https://github.com/vinnyshin/OperatingSystem/blob/master/project03/project3%20milestone%203.md)
Implemented pread and pwrite system calls to support file offset-based reading and writing.
