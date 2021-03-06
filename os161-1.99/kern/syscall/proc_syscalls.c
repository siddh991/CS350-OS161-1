#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <mips/trapframe.h> 
#include "opt-A2.h"
#include <synch.h>

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  #if OPT_A2

    // the plan: not much to do just set the exit code
    if (p->parent == NULL) {
        p->exited = true;
        proc_destroy(p); // just destroy it, it has no parent to worry about
    } else {
      // we making ourselves a zombie boys
      p->exitcode = _MKWAIT_EXIT(exitcode); 
      p->exited = true; // the parent will check for this when it destroys

      // we need to let our parent know that we're actually dead
      lock_acquire(p->cv_lock);
      cv_broadcast(p->cv, p->cv_lock);
      lock_release(p->cv_lock);
    }
  #endif

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
  
    //.~proc_destroy(p);
  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
  #if OPT_A2
  *retval = curproc->pid;
  #else 
  *retval = 1;
  #endif
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */
  if (options != 0) {
    return(EINVAL);
  }

 #if OPT_A2
  KASSERT(curproc != NULL);

  // only the parent can call on its children

  struct proc *child;

  // look for the matching child 
  for (unsigned i = 0; i < array_num(curproc->children); i++) {
    pid_t child_pid = ((struct proc*)array_get(curproc->children, i))->pid;
    if (child_pid == pid) {
      child = array_get(curproc->children, i); // we found it
      break;
    }
  }

  if (child == NULL) {
    panic ("PID not found");
    return ECHILD;
  }

  // if the child process hasn't exited, we have to block (how do we block?)
  lock_acquire(child->cv_lock);
  while (child->exited == false) {
    cv_wait(child->cv, child->cv_lock);
  }
  lock_release(child->cv_lock);
  exitstatus = child->exitcode;

  // if child process is exited, I need to get exit status and exit code
  result = copyout((void *)&exitstatus, status, sizeof(int));
  if (result) {
    return result;
  }
  *retval = pid;
  // can't rely on this to do PID cleanup

  // what if parent exits before children

#else
  /* for now, just pretend the exitstatus is 0 */
  exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
 #endif
  return(0);
}




#if OPT_A2
// we will return the child process' PID
  // what we need to do:
  // we need to create address space for the child (with as_copy())
  // we will use curproc_set() to assign the address space to the child
  // we need to assign the child a PID (lord knows how)
  // we have to create a thread for the child (use thread_fork())
  // we need to pass the trapframe to the child (apparently not using a pointer to it)
int sys_fork(struct trapframe *tf, pid_t *retval) {
  KASSERT(curproc != NULL);
  
    // create new child process
  struct proc *child = proc_create_runprogram(curproc->p_name);

  if (child == NULL) {
    return ENPROC;
  }

  /* create address space for the child */

  // copy from parent
  struct addrspace *child_addrspace = kmalloc(sizeof(struct addrspace)); 
  int copyret = as_copy(curproc_getas(), &child_addrspace);
  if (copyret != 0) {
    proc_destroy(child);
    return ENOMEM;
  }

  // make sure it copied properly
  if (child_addrspace == NULL) {
    return ENOMEM;
  }

  /* assign the address space to the child (copy paste from curproc_setas()) */

	spinlock_acquire(&child->p_lock);
	child->p_addrspace = child_addrspace;
	spinlock_release(&child->p_lock);

  /* give the child a PID */

  // the plan:
  //  we're going to give each process a pointer to its parent
  //  the parent will have a list of its children

  child->parent = curproc; // makes logical sense: I am the parent of my child

  array_init(child->children); // making sure my child is capable of children

  spinlock_acquire(&(curproc->p_lock));
  array_add(curproc->children, child, NULL); // add this child to my list of children
  spinlock_release(&(curproc->p_lock));

  /* create a thread for the child */

  /* change of plans, we're doing the trapframe first */
  struct trapframe *tf_child = kmalloc(sizeof(struct trapframe));;

  *tf_child = *tf;

  if (tf_child == NULL) {
    return ENOMEM;
  }
  // fork with name childproc, given the child process, and the entrypoint for forked processes
  int ret = thread_fork("childproc", child, ((void *)&enter_forked_process), tf_child, 0);

  if (ret) {
    kfree(tf_child);
    proc_destroy(child);
    return ret;
  }
  *retval = child->pid;
  return 0;
}
#endif
