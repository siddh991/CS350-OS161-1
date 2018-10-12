#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>
#include <array.h>

/*
* The game plan:
* 
* We're going to use the cv to stop a car from crossing 
* (blocking) until the conditions for it to go are met
* 
* We will keep a queue of all cars that we encounter.
* When the conditions are met, we will wake the next car 
* in the queue and it can then check if its conditions are met
* we do this until we find a car that can't enter.
*
* We only use the lock to prevent multiple cars from entering
* at the same time.
*
* 

*/
struct cv *intersection_cv;

struct lock *intersection_lock;

typedef struct vehicle {
  Direction origin;
  Direction destination;
} vehicle;

struct array *intersection_vehicles;


/* 
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 * 
 */
void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */

  // we use condition variables
  intersection_cv = cv_create("intersectionCV");
  if (intersection_cv == NULL) {
    panic("could not create intersection cv");
  }

  intersection_lock = lock_create("intersectionLock");
  if (intersection_lock == NULL) {
    panic("could not create intersection lock");
  }

  intersection_vehicles = array_create();
  array_init(intersection_vehicles);
  if (intersection_vehicles == NULL) {
    panic("Could not initialize vehicles array");
  }

  return;
}

/* 
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  /* replace this default implementation with your own implementation */
  KASSERT(intersection_cv != NULL);
  KASSERT(intersection_lock != NULL);
  KASSERT(intersection_vehicles != NULL);

  cv_destroy(intersection_cv);
  lock_destroy(intersection_lock);
  array_destroy(intersection_vehicles);
}


// determines if a right turn is being made
static bool is_right_turn(Direction origin, Direction destination) {
  if ((origin == north && destination == west) ||
      (origin == west && destination == south) ||
      (origin == south && destination == east) ||
      (origin == east && destination == north)) {
        return true;
      }
  return false;
}


// check if a new pair of origin, destination can enter based on current
static bool is_entry_valid(Direction currentOrigin, Direction currentDestination,
                           Direction newOrigin, Direction newDestination) {
  
  // if we're coming from the same direction
  if (currentOrigin == newOrigin) {
    return true;
  }

  // if we are going opposite directions
  if (newOrigin == currentDestination && newDestination == currentOrigin) {
    return true;
  }

  // if we're not going the same way, but one of us is turning right
  if ((newDestination != currentDestination) && 
  (is_right_turn(newOrigin, newDestination) || 
   is_right_turn(currentOrigin, currentDestination))) {
    return true;
   }

  // otherwise it's not valid
  return false;
}

static bool did_vehicle_enter(Direction origin, Direction destination) {

  for (unsigned int i = 0; i < array_num(intersection_vehicles); ++i) {
    // we go through each vehicle in the intersection to make sure there's no
    //    conflicts. If there is, we need to block the vehicle.

    vehicle *current = array_get(intersection_vehicles, i);
    if (!is_entry_valid(current->origin, current->destination, origin, destination)) {
      return false;
    }

  }

  // if we're at this point, we should hold the lock, and we can add the vehicle to the intersction
  KASSERT(lock_do_i_hold(intersection_lock));

  // create the cehicle
  vehicle* intersection_vehicle = kmalloc(sizeof(struct vehicle));
  KASSERT(intersection_vehicle != NULL);
  intersection_vehicle->origin = origin;
  intersection_vehicle->destination = destination;


  array_add(intersection_vehicles, intersection_vehicle, NULL);
  return true;
}

/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread 
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination) 
{

  KASSERT(intersection_cv != NULL);
  KASSERT(intersection_lock != NULL);
  KASSERT(intersection_vehicles != NULL);

  // we only want to consider one entry at a time, so we lock
  lock_acquire(intersection_lock);

  while(!did_vehicle_enter(origin, destination)) {
    // we wait
    cv_wait(intersection_cv, intersection_lock);
  }

  lock_release(intersection_lock);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination) 
{
  KASSERT(intersection_cv != NULL);
  KASSERT(intersection_lock != NULL);
  KASSERT(intersection_vehicles != NULL);

  lock_acquire(intersection_lock);

  // we're looking to remove this vehicle
  for (unsigned int i = 0; i < array_num(intersection_vehicles); ++i) {
    vehicle *current = array_get(intersection_vehicles, i);

    if (current->origin == origin && current->destination == destination) {
      array_remove(intersection_vehicles, i);

      // let every car know to check their conditions again
      cv_broadcast(intersection_cv, intersection_lock);
      break;
    }
  }

  lock_release(intersection_lock);
}
