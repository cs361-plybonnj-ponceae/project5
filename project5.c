/* CS 361 project5.c

Team: 07 
Names: Nic Plybon and Adrien Ponce 
Honor code statement: This code complies with the JMU Honor Code.
*/

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <mqueue.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "common.h"
#include "classify.h"
#include "intqueue.h"
#ifdef GRADING // do not delete this or the next two lines
#include "gradelib.h"
#endif

int main(int argc, char *argv[])
{
    int input_fd;
    int classification_fd;
    pid_t pid;
    off_t file_size;
    mqd_t tasks_mqd, results_mqd; // message queue descriptors
    char tasks_mq_name[16]; // name of the tasks queue
    char results_mq_name[18];   // name of the results queue
    int num_clusters;
    // Create mq attributes following specification
    struct mq_attr attributes;
    attributes.mq_flags = O_NONBLOCK;
    attributes.mq_maxmsg = 1000;
    attributes.mq_msgsize = MESSAGE_SIZE_MAX;
    attributes.mq_curmsgs = 0;
    
    // The user must supply a data file to be used as input
    if (argc != 2) {
        printf("Usage: %s data_file\n", argv[0]);
        return 1;
    }

    // Open input file for reading, exiting with error if open() fails
    input_fd = open(argv[1], O_RDONLY);
    if (input_fd < 0) {
        printf("Error opening file \"%s\" for reading: %s\n", argv[1], strerror(errno));
        return 1;
    }

    // Open classification file for writing. Create file if it does not
    // exist. Exit with error if open() fails.
    classification_fd = open(CLASSIFICATION_FILE, O_WRONLY | O_CREAT, 0600);
    if (classification_fd < 0) {
        printf("Error creating file \"%s\": %s\n", CLASSIFICATION_FILE, strerror(errno));
        return 1;
    }

    // Determine the file size of the input file
    file_size = lseek(input_fd, 0, SEEK_END);
    close(input_fd);
    // Calculate how many clusters are present
    num_clusters = file_size / CLUSTER_SIZE;

    // Generate the names for the tasks and results queue
    snprintf(tasks_mq_name, 16, "/tasks_%s", getlogin());
    tasks_mq_name[15] = '\0';
    snprintf(results_mq_name, 18, "/results_%s", getlogin());
    results_mq_name[17] = '\0';

    // Create the child processes
    for (int i = 0; i < NUM_PROCESSES; i++) {
        pid = fork();
        if (pid == -1)
            exit(1);
        else if (pid == 0) {

            // All the worker code must go here

            char cluster_data[CLUSTER_SIZE];
            char recv_buffer[MESSAGE_SIZE_MAX];
            struct task *my_task;
            unsigned char classification;
           
            // Here you need to create/open the two message queues for the
            // worker process. You must use the tasks_mqd and results_mqd
            // variables for this purpose
            if ((tasks_mqd = mq_open(tasks_mq_name, O_RDWR | O_CREAT, 0600, &attributes)) < 0) {
                printf("Error opening message queue %s: %s\n", tasks_mq_name, strerror(errno));
                return 1;
            }
            if ((results_mqd = mq_open(results_mq_name, O_RDWR | O_CREAT, 0600, &attributes)) < 0) {
                printf("Error opening message queue %s: %s\n", results_mq_name, strerror(errno));
                return 1;
            }
            // At this point, the queues must be open
#ifdef GRADING // do not delete this or you will lose points
            test_mqdes(tasks_mqd, "Tasks", getpid());
            test_mqdes(results_mqd, "Results", getpid());
#endif
            // A worker process must endlessly receive new tasks
            // until instructed to terminate
            while(1) { 
                // receive the next task message here; you must read into recv_buffer
                if (mq_receive(tasks_mqd, recv_buffer, attributes.mq_msgsize, NULL) < 0) {
                    printf("Error receiving message from %s: %s\n", tasks_mq_name, strerror(errno));
                    return 1;
                }      
                // cast the received message to a struct task
                my_task = (struct task *)recv_buffer;
                switch (my_task->task_type) {
                    case TASK_CLASSIFY:
#ifdef GRADING // do not delete this or you will lose points
                        printf("(%d): received TASK_CLASSIFY\n", getpid());
#endif
                        // you must retrieve the data for the specified cluster
                        // and store it in cluster_data before executing the
                        // code below

                        // Open input file for reading, exiting with error if open() fails
                        input_fd = open(argv[1], O_RDONLY);
                        if (input_fd < 0) {
                            printf("Error opening file \"%s\" for reading: %s\n", argv[1], strerror(errno));
                            return 1;
                        }

                        // seek to the specified cluster in the input file
                        lseek(input_fd, 0, SEEK_SET);
                        lseek(input_fd, my_task->task_cluster*CLUSTER_SIZE, SEEK_SET);

                        // read the cluster data 
                        read(input_fd, &cluster_data, CLUSTER_SIZE);
                        
                        // Call the classifier functions on the cluster data and generate the classification type

                        // Classification code
                        classification = TYPE_UNCLASSIFIED;

                        if (has_jpg_body(cluster_data)) {
                            classification |= TYPE_IS_JPG;
                        }
                        if (has_jpg_header(cluster_data)) {
                            classification |= TYPE_JPG_HEADER | TYPE_IS_JPG;
                        }
                        if (has_jpg_footer(cluster_data)) {
                            classification |= TYPE_JPG_FOOTER | TYPE_IS_JPG;
                        }
                        // In the below statement, none of the JPG types were set, so
                        // the cluster must be HTML
                        if (classification == TYPE_UNCLASSIFIED) {
                            if (has_html_body(cluster_data)) {
                                classification |= TYPE_IS_HTML;
                            }
                            if (has_html_header(cluster_data)) {
                                classification |= TYPE_HTML_HEADER | TYPE_IS_HTML;
                            }
                            if (has_html_footer(cluster_data)) {
                                classification |= TYPE_HTML_FOOTER | TYPE_IS_HTML;
                            }
                        }
                        if (classification == TYPE_UNCLASSIFIED) {
                            classification = TYPE_UNKNOWN;
                        }

                        // printf("%d\n", classification);

                        // generate the classification message (struct result) and send to result queue
                        struct result result;
                        result.res_cluster_number = my_task->task_cluster;
                        result.res_cluster_type = classification;

                        // printf("cluster number:%d cluster type:%d\n", result.res_cluster_number, result.res_cluster_type);
                        
                        // send it to results queue
                        if (errno == EAGAIN) {
                            if (mq_send(results_mqd, (const char *) &result, sizeof(result), 0) < 0) {
                                printf("Error sending to results queue: %s\n", strerror(errno));
                                return 1;
                            }
                        } 
                        break;

                    case TASK_MAP:

#ifdef GRADING // do not delete this or you will lose points
                        printf("(%d): received TASK_MAP\n", getpid());
#endif

                        // implement the map task logic here


                        break;

                    default:

#ifdef GRADING // do not delete this or you will lose points
                        printf("(%d): received TASK_TERMINATE or invalid task\n", getpid());
#endif
                        // implement the terminate task logic here
                        mq_close(tasks_mqd);
                        mq_close(results_mqd);
                        // printf("TERMINATE\n");

                        exit(0);
                }
            }
        }
    }

    // All the supervisor code needs to go here
    
    struct intqueue headerq;

    // Initialize an empty queue to store the clusters that have file headers.
    // This queue needs to be populated in Phase 1 and worked off in Phase 2.
    initqueue(&headerq);

    
    // Here you need to create/open the two message queues for the
    // supervisor process. You must use the tasks_mqd and results_mqd
    // variables for this purpose
    if ((tasks_mqd = mq_open(tasks_mq_name, O_RDWR | O_CREAT, 0600, &attributes)) < 0) {
        printf("Error opening message queue %s: %s\n", tasks_mq_name, strerror(errno));
        return 1;
    }
    if ((results_mqd = mq_open(results_mq_name, O_RDWR | O_CREAT, 0600, &attributes)) < 0) {
        printf("Error opening message queue %s: %s\n", results_mq_name, strerror(errno));
        return 1;
    }

    // At this point, the queues must be open
#ifdef GRADING // do not delete this or you will lose points
    test_mqdes(tasks_mqd, "Tasks", getpid());
    test_mqdes(results_mqd, "Results", getpid());
#endif
    // Implement Phase 1 here

    struct result result;
    
    // receive the next message from the results queue
    // mq_receive(results_mqd, (char *) &result, attributes.mq_msgsize, NULL);
    
    // seek to the correct location in the classification file as specified by res_cluster_number

    // write the classification type to the location

    // if the classification type indicates a file header, the cluster number is also saved in a 
    // queue for use in Phase 2. You must use the provided intqueue queue data structure for this purpose.

    struct task classify;
    classify.task_type = TASK_CLASSIFY;
    classify.task_cluster = 0;
    
    for (int i = num_clusters; i > 0; i--) {
        // printf("%s", "HERE");
        
        // Generate new tasks and send it to the tasks queue
        if (mq_send(tasks_mqd, (const char *) &classify, sizeof(classify), 0) < 0) {
            
            // Tasks queue is empty
            if (errno != EAGAIN) {
                // char recv_buffer[MESSAGE_SIZE_MAX];
                // result.res_cluster_number = task.task_cluster;
                // result.res_cluster_type = classification;
                if (mq_receive(results_mqd, (char *) &result, sizeof(result), NULL) < 0) {
                    printf("Error receiving message from results queue: %s\n", strerror(errno));
                    return 1;
                } 
                printf("%d\n", result.res_cluster_number);

                lseek(classification_fd, result.res_cluster_number, SEEK_SET);
            }
            printf("Error sending to tasks queue: %s\n", strerror(errno));
        return 1;   
        }
        classify.task_cluster++;
        num_clusters--;
    }
    // End of Phase 1

#ifdef GRADING // do not delete this or you will lose points
    printf("(%d): Starting Phase 2\n", getpid());
#endif

    // Here you need to swtich the tasks queue to blocking

#ifdef GRADING // do not delete this or you will lose points
    test_mqdes(tasks_mqd, "Tasks", getpid());
#endif


    // Implement Phase 2 here



    // End of Phase 2

#ifdef GRADING // do not delete this or you will lose points
    printf("(%d): Starting Phase 3\n", getpid());
#endif

    // Implement Phase 3 here
    // generate a terminate task for each process
    struct task terminate;
    terminate.task_type = TASK_TERMINATE;
    for (int i = NUM_PROCESSES; i > 0; i--) {
        // send to tasks queue
        if (mq_send(tasks_mqd, (const char *) &terminate, sizeof(terminate), 0) < 0) {
            printf("Error sending to tasks queue: %s\n", strerror(errno));
            return 1;
        }
    }
    // Wait for children to terminate
    wait(NULL);
    // Close the input and classification files
    close(input_fd);
    close(classification_fd);
    //close any open mqds
    mq_close(tasks_mqd);
    mq_close(results_mqd);
    //unlink mqueues
    mq_unlink(tasks_mq_name);
    mq_unlink(results_mq_name);
    //terminates itself
    return 0;
};
