/* The Channel class, provides a simple, Ruby thread safe communication
 * mechanism. */

#include "vm.hpp"
#include "vm/object_utils.hpp"
#include "objectmemory.hpp"

#include "builtin/object.hpp"
#include "builtin/channel.hpp"
#include "builtin/thread.hpp"
#include "builtin/list.hpp"
#include "builtin/fixnum.hpp"
#include "builtin/float.hpp"
#include "builtin/io.hpp"

#include "arguments.hpp"
#include "dispatch.hpp"
#include "call_frame.hpp"

#include "on_stack.hpp"

#include <sys/time.h>

namespace rubinius {

  void Channel::init(STATE) {
    GO(channel).set(state->new_class("Channel", G(object), G(rubinius)));
    G(channel)->set_object_type(state, Channel::type);
    G(channel)->name(state, state->symbol("Rubinius::Channel"));
  }

  Channel* Channel::create(STATE) {
    Channel* chan = state->new_object_mature<Channel>(G(channel));
    chan->waiters_ = 0;
    chan->semaphore_count_ = 0;

    // Using placement new to call the constructor of condition_
    new(&chan->condition_) thread::Condition();
    new(&chan->mutex_) thread::Mutex();

    chan->value(state, List::create(state));

    return chan;
  }

  /** @todo Remove the event too? Should not affect code, but no need for it either. --rue */
  void Channel::cancel_waiter(STATE, const Thread* waiter) {
    // waiting_->remove(state, waiter);
  }

  Object* Channel::send(STATE, Object* val) {
    GCLockGuard lg(state, mutex_);

    if(val->nil_p()) {
      semaphore_count_++;
    } else {
      if(semaphore_count_ > 0) {
        for(int i = 0; i < semaphore_count_; i++) {
          value_->append(state, Qnil);
        }
        semaphore_count_ = 0;
      }

      value_->append(state, val);
    }

    if(waiters_ > 0) {
      condition_.signal();
    }

    return Qnil;
  }

  Object* Channel::try_receive(STATE) {
    GCLockGuard lg(state, mutex_);

    if(semaphore_count_ > 0) {
      semaphore_count_--;
      return Qnil;
    }

    if(value_->empty_p()) return Qnil;
    return value_->shift(state);
  }

  Object* Channel::receive(STATE, CallFrame* call_frame) {
    return receive_timeout(state, Qnil, call_frame);
  }

#define NANOSECONDS 1000000000
  Object* Channel::receive_timeout(STATE, Object* duration, CallFrame* call_frame) {
    GCLockGuard lg(state, mutex_);

    if(semaphore_count_ > 0) {
      semaphore_count_--;
      return Qnil;
    }

    if(!value_->empty_p()) return value_->shift(state);

    // Otherwise, we need to wait for a value.
    struct timespec ts = {0,0};
    bool use_timed_wait = true;

    if(Fixnum* fix = try_as<Fixnum>(duration)) {
      ts.tv_sec = fix->to_native();
    } else if(Float* flt = try_as<Float>(duration)) {
      uint64_t nano = (uint64_t)(flt->val * NANOSECONDS);
      ts.tv_sec  =  (time_t)(nano / NANOSECONDS);
      ts.tv_nsec =    (long)(nano % NANOSECONDS);
    } else if(duration->nil_p()) {
      use_timed_wait = false;
    } else {
      return Primitives::failure();
    }

    // Passing control away means that the GC might run. So we need
    // to stash this into a root, and read it back out again after
    // control is returned.
    //
    // DO NOT USE this AFTER wait().

    // We have to do this because we can't pass this to OnStack, since C++
    // won't let us reassign it.
    Channel* self = this;
    OnStack<1> sv(state, self);

    // We pin this so we can pass condition_ out without worrying about
    // us moving it.
    if(!this->pin()) {
      rubinius::bug("unable to pin Channel");
    }

    struct timeval tv = {0,0};
    if(use_timed_wait) {
      gettimeofday(&tv, 0);
      uint64_t nano = ts.tv_nsec + tv.tv_usec * 1000;
      ts.tv_sec  += tv.tv_sec + nano / NANOSECONDS;
      ts.tv_nsec  = nano % NANOSECONDS;
    }

    waiters_++;

    state->wait_on_channel(this);

    for(;;) {
      {
        GCIndependent gc_guard(state, call_frame);

        if(use_timed_wait) {
          if(condition_.wait_until(mutex_, &ts) == thread::cTimedOut) break;
        } else {
          condition_.wait(mutex_);
        }
      }

      // or there are values available.
      if(self->semaphore_count_ > 0 || !self->value()->empty_p()) break;
    }

    state->clear_waiter();
    state->thread->sleep(state, Qfalse);

    self->unpin();
    self->waiters_--;

    if(!state->check_async(call_frame)) return NULL;

    if(semaphore_count_ > 0) {
      semaphore_count_--;
      return Qnil;
    }

    // We were awoken, but there is no value to use. Return false.
    if(self->value()->empty_p()) return Qfalse;

    return self->value()->shift(state);
  }


  bool Channel::has_readers_p() {
    return waiters_ > 0;
  }

  class SendToChannel : public ObjectCallback {
  public:
    TypedRoot<Channel*> chan;

    SendToChannel(STATE, Channel* chan) : ObjectCallback(state), chan(state, chan) { }

    virtual Object* object() {
      return chan.get();
    }

    virtual void call(Object* obj) {
      chan->send(state, obj);
    }
  };

/* ChannelCallback */

  ChannelCallback::ChannelCallback(STATE, Channel* chan) : ObjectCallback(state) {
    channel.set(chan, &GO(roots));
  }

  void ChannelCallback::call(Object* obj) {
    channel->send(state, obj);
  }
}
