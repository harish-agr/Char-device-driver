/* ----------------------------------------------- DRIVER bus ---------------------------------------------------
 
 Driver containing four devices to handle four queues.
 
 ----------------------------------------------------------------------------------------------------------------*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include<linux/init.h>
#include<linux/moduleparam.h>

#define INPUT_DEVICE_NAME 	"bus_in_q"  	/* Device name input queue */
#define OUTPUT1_DEVICE_NAME	"bus_out_q1"	/* Device name for output queue1 */
#define OUTPUT2_DEVICE_NAME	"bus_out_q2"	/* Device name for output queue2 */
#define OUTPUT3_DEVICE_NAME	"bus_out_q3"	/* Device name for output queue3 */
#define MAX_MSG_SIZE		80		/* Maximum size of message is 80 */
#define MAX_BUS_SIZE		10		/* Maximum bus Size is 10 */
#define SUCCESS			0		/* Macro for Success */
#define FAILURE			-1		/* Macro for Failure */

/*Device message structure */
struct dev_msg {
	int messageId;			/* Message Id */
	int sourceId;			/* Source Id*/
	int destinationId;		/* Destination Id */
	char msgString[80];		/* Message string of max size 80 */
	int msgLength;			/* Length of the Message */
	unsigned long  long enqtime;	/* Enqueue Time stamp */
	unsigned long long timeStamp;	/* Time Stamp */
};

/* per device structure */
struct bus_dev {
	struct cdev cdev;               /* The cdev structure */
	char name[20];                  /* Name of device*/
	struct dev_msg *messages[10];	/* Queue of messages */
	int front;			/* Front of the queue */
	int rear;			/* Rear of the queue */
	struct mutex busMutex;		/* Mutex for the device */
} *bus_in_q, *bus_out_q1, *bus_out_q2, *bus_out_q3;

static dev_t bus_q_dev_number;		/* Allotted device number for bus_in_q */

struct class *bus_in_q_dev_class;          /* Tie with the device model */
struct class *bus_out_q1_dev_class;
struct class *bus_out_q2_dev_class;
struct class *bus_out_q3_dev_class;
	
static struct device *bus_in_q_dev_device;
static struct device *bus_out_q1_dev_device;
static struct device *bus_out_q2_dev_device;
static struct device *bus_out_q3_dev_device;

//static char *user_name = "Amaresh";

/* Function Prototypes */
int bus_driver_msg_enqueue(struct bus_dev *bus, struct dev_msg *newMsg);
int bus_driver_msg_dequeue(struct bus_dev *bus, struct dev_msg *outputMsg);

/*
* Open gmem driver
*/
int bus_driver_open(struct inode *inode, struct file *file){
	struct bus_dev *bus;

	/* Get the per-device structure that contains this cdev */
	bus = container_of(inode->i_cdev, struct bus_dev, cdev);

	/* Easy access to cmos_devp from rest of the entry points */
	file->private_data = bus;
	
	//printk("\n%s is opening", bus->name);
	return 0;
}

/*
 * Release bus driver
 */
int bus_driver_release(struct inode *inode, struct file *file){
//	struct bus_dev *bus = file->private_data;
	
	//printk("\n%s is closing", bus->name);
	return 0;
}

/*
 * Write to bus driver
 */
ssize_t bus_driver_write(struct file *file, const char *buf,
           size_t count, loff_t *ppos){
	int retVal = FAILURE;
	struct bus_dev *bus = file->private_data;
	struct dev_msg *newMessage = (struct dev_msg*) buf;
	
	/* Enqueue the input Queue with the new message */
	retVal = bus_driver_msg_enqueue(bus, newMessage);
		
	/* Check the return value */
	if(retVal == SUCCESS){
		//printk("\nName - %s Front - %d Rear - %d", bus->name, bus->front, bus->rear);
		return SUCCESS;
	}
	else{
		return -EINVAL;
	}
	
}

/*
 * Enqueuing the Queue
 */
int bus_driver_msg_enqueue(struct bus_dev *bus, struct dev_msg *newMsg){
	int count = 0;
	unsigned cycles_low = 0;
	unsigned cycles_high = 0;
	
	mutex_lock(&bus->busMutex);

	/* Check if the Queue is Full */
	if(bus->front == 0 && bus->rear == MAX_BUS_SIZE - 1){
		//printk("%s is full", bus->name);		

		/* Unlock mutex */		
		mutex_unlock(&bus->busMutex);

		return FAILURE;
	}
	
	/* Update front and rear */
	if(bus->front == -1){
		bus->front = 0;
		bus->rear = 0;
	}
	else{
		bus->rear = (bus->rear + 1) % MAX_BUS_SIZE;		
	}

	/* Allocate memory for message */
	bus->messages[bus->rear] = kmalloc(sizeof(struct dev_msg), GFP_KERNEL);

	/* Assign Message Id */
	bus->messages[bus->rear]->messageId = newMsg->messageId;

	/* Assign the destination Id */	
	bus->messages[bus->rear]->destinationId = newMsg->destinationId;
					
	/* Assign the length of message */	
	bus->messages[bus->rear]->msgLength = newMsg->msgLength;
	
	/* Assign the enque time*/
	if(strcmp(bus->name,"bus_in_q") == 0){
		__asm__ volatile (
			"CPUID\n\t"
			"RDTSC\n\t"
			"mov %%edx, %0\n\t"
			"mov %%eax, %1\n\t": "=r" (cycles_high), "=r"
			(cycles_low):: "%rax", "%rbx", "%rcx", "%rdx");
		bus->messages[bus->rear]->enqtime =  ((uint64_t)cycles_high << 32) | cycles_low;
		bus->messages[bus->rear]->timeStamp = newMsg->timeStamp;
//		//printk("\ntime spant is %lu", bus->messages[bus->rear]->enqtime);
	}

	/* Print the Message being written */
	//printk("\nName %s Message Id %d Writing -- ", bus->name, bus->messages[bus->rear]->messageId);

	while(count < (newMsg->msgLength)){
		get_user(bus->messages[bus->rear]->msgString[count], &(newMsg->msgString[count]));
		//printk("%c", bus->messages[bus->rear]->msgString[count]);
		count++;

		if(count < newMsg->msgLength){
			/* If the message is more that length 80 return FAILURE */
			if(bus->messages[bus->rear]->msgLength == MAX_MSG_SIZE){
				bus->messages[bus->rear]->msgLength = 0;		
				//printk("\n write failed in msg length - %s \n", bus->name);				

				/* Mutex Unloack */			
				mutex_unlock(&bus->busMutex);
				return FAILURE;
			}
		}
	}

	/* Unlocak Mutex */
	mutex_unlock(&bus->busMutex);
	return SUCCESS;
}

/*
 * Read to bus driver
 */
ssize_t bus_driver_read(struct file *file, char *buf,
           size_t count, loff_t *ppos){
	int outputMsgSize = 0;
	int retVal = FAILURE;
	struct bus_dev *bus = file->private_data;
	struct dev_msg *outputMsg = (struct dev_msg*) buf;
	
	/* Dequeue the message */
	retVal = bus_driver_msg_dequeue(bus, outputMsg);
	if(retVal == FAILURE){
		return -EINVAL;
	}
	else{
		//printk("\nName - %s Front - %d Rear - %d", bus->name, bus->front, bus->rear);
		outputMsgSize = retVal;
	}
	
	/* Return back the size of the Output message structure */
	return outputMsgSize;
}

/*
 * Dequeuing the Queue
 */
int bus_driver_msg_dequeue(struct bus_dev *bus, struct dev_msg *outputMsg){
	int bytesRead = 0;
	unsigned cycles_low = 0;
	unsigned cycles_high = 0;

	/* Lock the Mutex */
	mutex_lock(&bus->busMutex);

	/* Check if bus is empty */	
	if(bus->front == -1){
		//printk("\n%s is empty", bus->name);
		
		mutex_unlock(&bus->busMutex);		
		return FAILURE;
	}
	
	/* Copy the message Id */
	outputMsg->messageId = bus->messages[bus->front]->messageId;

	/* Copy the source Id */
	outputMsg->sourceId = bus->messages[bus->front]->sourceId;

	/* Copy the Destination Id */
	outputMsg->destinationId = bus->messages[bus->front]->destinationId;
	
	/* Copy the Message Length */
	outputMsg->msgLength = bus->messages[bus->front]->msgLength;

	/* Copy initial time stamp */
	outputMsg->enqtime = bus->messages[bus->front]->enqtime;
	
	/* Calculate the time stamp */
	__asm__ volatile (
		"CPUID\n\t"
		"RDTSC\n\t"
		"mov %%edx, %0\n\t"
		"mov %%eax, %1\n\t": "=r" (cycles_high), "=r"
		(cycles_low):: "%rax", "%rbx", "%rcx", "%rdx");
	outputMsg->timeStamp = (outputMsg->timeStamp + (((uint64_t)cycles_high << 32) | cycles_low)) - outputMsg->enqtime;
	
	/* Print the message read */
	//printk("\nName %s Messge Id %d Reading -- ", bus->name, outputMsg->messageId);

	/* Read from the Queue */
	while (bytesRead < bus->messages[bus->front]->msgLength) {
		put_user(bus->messages[bus->front]->msgString[bytesRead], &(outputMsg->msgString[bytesRead]));
		//printk("%c",bus->messages[bus->front]->msgString[bytesRead]);
		bytesRead++;
	}

	/* Free the allocated memory */	
	kfree(bus->messages[bus->front]);

	/* Update the front and rear value */
	if(bus->front == bus->rear){
		bus->front = bus->rear = -1;
	}
	else{
		bus->front = (bus->front + 1) % MAX_BUS_SIZE;		
	}

	/* Unlock Mutex */		
	mutex_unlock(&bus->busMutex);		

	return (sizeof(outputMsg));
}

/* File operations structure. Defined in linux/fs.h */
static struct file_operations bus_fops = {
    .owner		= THIS_MODULE,		/* Owner */
    .open		= bus_driver_open,	/* Open method */
    .release		= bus_driver_release,	/* Release method */
    .write		= bus_driver_write,	/* Write method */
    .read		= bus_driver_read,	/* Read method */
};

/*
 * Driver Initialization
 */
int __init bus_driver_init(void){
	int ret;

	/* Request dynamic allocation of a device major number for bus_in_q */
	if (alloc_chrdev_region(&bus_q_dev_number, 0, 4, INPUT_DEVICE_NAME) < 0) {
			//printk(KERN_DEBUG "Can't register device\n"); return -1;
	}

	/* Populate sysfs entries */
	bus_in_q_dev_class = class_create(THIS_MODULE, INPUT_DEVICE_NAME);
	bus_out_q1_dev_class = class_create(THIS_MODULE, OUTPUT1_DEVICE_NAME);
	bus_out_q2_dev_class = class_create(THIS_MODULE, OUTPUT2_DEVICE_NAME);
	bus_out_q3_dev_class = class_create(THIS_MODULE, OUTPUT3_DEVICE_NAME);
	
	/* Allocate memory for the per-device structure */
	bus_in_q = kmalloc(sizeof(struct bus_dev), GFP_KERNEL);
	bus_out_q1 = kmalloc(sizeof(struct bus_dev), GFP_KERNEL);
	bus_out_q2 = kmalloc(sizeof(struct bus_dev), GFP_KERNEL);
	bus_out_q3 = kmalloc(sizeof(struct bus_dev), GFP_KERNEL);
		
	/* Check if memory allocation failed */
	if (!bus_in_q || !bus_out_q1 || !bus_out_q2 || !bus_out_q3) {
		//printk("Bad Kmalloc\n"); return -ENOMEM;
	}

	/* Request I/O region for all devices*/
	sprintf(bus_in_q->name, INPUT_DEVICE_NAME);
	sprintf(bus_out_q1->name, OUTPUT1_DEVICE_NAME);
	sprintf(bus_out_q2->name, OUTPUT2_DEVICE_NAME);
	sprintf(bus_out_q3->name, OUTPUT3_DEVICE_NAME);

	/* Connect the file operations with the cdev */
	cdev_init(&bus_in_q->cdev, &bus_fops);
	cdev_init(&bus_out_q1->cdev, &bus_fops);
	cdev_init(&bus_out_q2->cdev, &bus_fops);
	cdev_init(&bus_out_q3->cdev, &bus_fops);

	/* Assign the owner for each device */
	bus_in_q->cdev.owner = THIS_MODULE;
	bus_out_q1->cdev.owner = THIS_MODULE;
	bus_out_q2->cdev.owner = THIS_MODULE;
	bus_out_q3->cdev.owner = THIS_MODULE;

	/* Connect the major/minor number of bus_in_q to the cdev */
	ret = cdev_add(&bus_in_q->cdev, (bus_q_dev_number), 1);
	if (ret) {
		printk("Bad cdev in q\n");
		return ret;
	}

	/* Connect the major/minor number of bus_out_q1 to the cdev */
	ret = cdev_add(&bus_out_q1->cdev, (bus_q_dev_number) + 1, 1);
	if (ret) {
		printk("Bad cdev out q1\n");
		return ret;
	}

	/* Connect the major/minor number of bus_out_q2 to the cdev */
	ret = cdev_add(&bus_out_q2->cdev, (bus_q_dev_number) + 2, 1);
	if (ret) {
		printk("Bad cdev out q2\n");
		return ret;
	}

	/* Connect the major/minor number of bus_out_q3 to the cdev */
	ret = cdev_add(&bus_out_q3->cdev, (bus_q_dev_number) + 3, 1);
	if (ret) {
		printk("Bad cdev out q3\n");
		return ret;
	}
	
	/* Send uevents to udev, so it'll create /dev nodes */
	bus_in_q_dev_device = device_create(bus_in_q_dev_class, NULL, MKDEV(MAJOR(bus_q_dev_number), 0), NULL, INPUT_DEVICE_NAME);
	bus_out_q1_dev_device = device_create(bus_out_q1_dev_class, NULL, MKDEV(MAJOR(bus_q_dev_number), 1), NULL, OUTPUT1_DEVICE_NAME);
	bus_out_q2_dev_device = device_create(bus_out_q2_dev_class, NULL, MKDEV(MAJOR(bus_q_dev_number), 2), NULL, OUTPUT2_DEVICE_NAME);
	bus_out_q3_dev_device = device_create(bus_out_q3_dev_class, NULL, MKDEV(MAJOR(bus_q_dev_number), 3), NULL, OUTPUT3_DEVICE_NAME);
	
	/* Initialize front, rear and mutex of bus_in_q */
	bus_in_q->front = -1;
	bus_in_q->rear = -1;
	mutex_init(&bus_in_q->busMutex);
	
	/* Initialize front, rear and mutex of bus_out_q1 */
	bus_out_q1->front = -1;
	bus_out_q1->rear = -1;
	mutex_init(&bus_out_q1->busMutex);

	/* Initialize front, rear and mutex of bus_out_q2 */
	bus_out_q2->front = -1;
	bus_out_q2->rear = -1;
	mutex_init(&bus_out_q2->busMutex);

	/* Initialize front, rear and mutex of bus_out_q3 */
	bus_out_q3->front = -1;
	bus_out_q3->rear = -1;
	mutex_init(&bus_out_q3->busMutex);

	//printk("Mydriver driver initialized.\n");
	return 0;
}

/* Driver Exit */
void __exit bus_driver_exit(void){
	// device_remove_file(bus_in_q_dev_device, &dev_attr_xxx);
	
	/* Release the major number */
	unregister_chrdev_region((bus_q_dev_number), 1);

	/* Destroy all devices */
	/* Destroy bus_in_q */
	device_destroy (bus_in_q_dev_class, MKDEV(MAJOR(bus_q_dev_number), 0));
	cdev_del(&bus_in_q->cdev);
	kfree(bus_in_q);

	/* Destroy bus_out_q1 */
	device_destroy (bus_out_q1_dev_class, MKDEV(MAJOR(bus_q_dev_number), 1));
	cdev_del(&bus_out_q1->cdev);
	kfree(bus_out_q1);

	/* Destroy bus_out_q2 */
	device_destroy (bus_out_q2_dev_class, MKDEV(MAJOR(bus_q_dev_number), 2));
	cdev_del(&bus_out_q2->cdev);
	kfree(bus_out_q2);

	/* Destroy bus_out_q3 */
	device_destroy (bus_out_q3_dev_class, MKDEV(MAJOR(bus_q_dev_number), 3));
	cdev_del(&bus_out_q3->cdev);
	kfree(bus_out_q3);
	
	/* Destroy driver_class */
	class_destroy(bus_in_q_dev_class);
	class_destroy(bus_out_q1_dev_class);
	class_destroy(bus_out_q2_dev_class);
	class_destroy(bus_out_q3_dev_class);

	//printk("Mydriver driver removed.\n");
}

module_init(bus_driver_init);
module_exit(bus_driver_exit);
MODULE_LICENSE("GPL v2");
