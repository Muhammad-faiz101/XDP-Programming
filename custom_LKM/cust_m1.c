
#include<linux/kernel.h>
#include<linux/init.h>
#include<linux/module.h>

MODULE_DESCRIPTION("test cust mod3");
MODULE_AUTHOR("bushra");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");

static int __init b_init(void){ 
    printk(KERN_INFO "custom mod loaded!\n");
    return 0;
}

static void __exit b_exit(void){
    printk(KERN_INFO "custom mod unloaded now\n"); //alt. to printk(KERN_INFO ..) is pr_info
}

module_init(b_init);
module_exit(b_exit);

