// Emacs Mode Line: -*- Mode:c++;-*-
// -------------------------------------------------------------
/*
 *     Copyright (c) 2013 Battelle Memorial Institute
 *     Licensed under modified BSD License. A copy of this license can be found
 *     in the LICENSE file in the top level directory of this distribution.
 */
// -------------------------------------------------------------
/**
 * @file   dae_event.hpp
 * @author Perkins
 * @date   2019-11-20 14:43:16 d3g096
 * 
 * @brief  Encapsulate an "Event" that would affect a time integration problem
 * 
 * This is based primarily on PETSc's TSEvent model.  It may not be
 * general enough.  
 * 
 */
// -------------------------------------------------------------
// Created November 18, 2019 by Perkins
// Last Change: 2018-07-24 09:27:04 d3g096
// -------------------------------------------------------------


#ifndef _dae_event_hpp_
#define _dae_event_hpp_

#include <vector>
#include <list>
#include <boost/scoped_array.hpp>
#include <boost/foreach.hpp>
#include <gridpack/math/vector.hpp>
#include <gridpack/utilities/uncopyable.hpp>

namespace gridpack {
namespace math {

// -------------------------------------------------------------
// enum DAEEventDirection
// -------------------------------------------------------------
enum DAEEventDirection
  {
   CrossZeroNegative = -1,
   CrossZeroPositive = 1,
   CrossZeroEither = 0
  };



// -------------------------------------------------------------
// abstract class DAEEventT
// -------------------------------------------------------------
/// Description of zero events and how to handle them
/**
 * This type knows how to compute one or more event values and adjust
 * the integration problem when any of those values are triggered
 * 
 */

template <typename T, typename I = int>
class DAEEventT
  : private utility::Uncopyable
{
public:
  /// Default constructor.
  /** 
   * A single instance can handle multiple "events". 
   * 
   * @param n number of events handled by this instance
   * @param dirs the directions of each event
   * 
   * @return new instance
   */
  DAEEventT(const int& nevent)
    : utility::Uncopyable(),
      p_stateIndex(0),
      p_size(nevent), 
      p_dir(p_size, CrossZeroEither), // filled by children
      p_current(p_size, -9999.0) // filled by children, just make sure it's not zero initially
  {
  }

  /// Destructor
  virtual ~DAEEventT(void)
  {}

  /// get the size of this event
  int size(void) const
  {
    return p_size;
  }

  /// get the required index into the @em local state vector
  int stateIndex(void) const
  {
    return p_stateIndex;
  }

  /// get the directions of this event
  DAEEventDirection const *dirs(void) const
  {
    return &p_dir[0];
  }

  /// get the current values
  T const *values(void) const
  {
    return &p_current[0];
  }

  /// update and return event values, given the state
  T const *values(const double& t, T *state)
  {
    this->p_update(t, state);
    return this->values();
  }

  /// handle any triggered events
  void handle(const bool *triggered, const double& t, T *state)
  {
    this->p_handle(triggered, t, state);
  }

protected:

  /// The number of event values handled by this instance
  const int p_size;

  /// The base index into the @em local solution index
  int p_stateIndex;

  /// The direction which event value passes zero
  std::vector<DAEEventDirection> p_dir;

  /// Current event values
  std::vector<T> p_current;
  
  /// update and return event values, given the state (specialized)
  /** 
   * Fill #p_current with values base on the current #state
   * 
   * @param t 
   * @param T 
   * @param state 
   */
  virtual void p_update(const double& t, T *state) = 0;

  /// handle any triggered events  (specialized)
  /** 
   * This does whatever is necessary to adjust the internals of this
   * instance based on the events that were triggered.
   * 
   * @param triggered array of length #p_size that indicate which events occurred
   * @param t solver time
   * @param state current solver state
   */
  virtual void p_handle(const bool *triggered, const double& t, T *state) = 0;
};

// -------------------------------------------------------------
// class DAEEventManagerT
// -------------------------------------------------------------
template <typename T, typename I = int>
class DAEEventManagerT
  : private utility::Uncopyable
{
public:

  /// Default constructor.
  DAEEventManagerT(void);

  /// Destructor
  virtual ~DAEEventManagerT(void);

  typedef DAEEventT< T, I > Event; 
  typedef boost::shared_ptr< Event > EventPtr;

  /// Add an event
  void add(EventPtr e)
  {
    int nextidx;
    if (!p_events.empty()) {
      nextidx = p_events.back().evidx;
      nextidx += p_events.back().nval;
    } else {
      nextidx = 0;
    }
    
    p_EventInfo rec;
    rec.event = e;
    rec.nval = e->size();
    rec.evidx = nextidx;
    p_events.push_back(rec);
    p_size = -1;
    p_dirs.reset();
    p_values.reset();
    p_trigger.reset();
  }

  /// Get the number of event values handled
  int size(void) const
  {
    int n(0);
    if (p_size >= 0) {
      n = p_size;
    } else {
      BOOST_FOREACH(p_EventInfo rec, p_events) {
        n += rec.nval;
      }
      p_size = n;
    }
    return n;
  }

  /// Get all the event directions
  DAEEventDirection const *
  directions(void) const
  {
    int n(this->size());
    if (!p_dirs) {
      p_dirs.reset(new DAEEventDirection[n]);
    }

    n = 0;
    BOOST_FOREACH(p_EventInfo rec, p_events) {
      const DAEEventDirection *d((rec.event)->dirs());
      int pn(rec.nval);
      for (int i = 0; i < pn; ++i) {
        p_dirs[n++] = d[i];
      }
    }
    return p_dirs.get();
  }

  /// Update and get the current event values
  T const *
  values(const double t, VectorT<T, I>& state)
  {
    int n(this->size());
    if (!p_values) {
      p_values.reset(new T[n]);
    }

    T *lstate = state.getLocalElements();

    n = 0;
    BOOST_FOREACH(p_EventInfo rec, p_events) {
      int lidx(rec.solidx);
      const T *v((rec.event)->update(t, &lstate[lidx]));
      for (int i = 0; i < rec.nvalue; ++i) {
        p_values[n++] = v[i];
      }
    }

    state.releaseLocalElements(lstate);
    
    return p_values.get();
  }

  /// Handle triggered events
  void
  handle(const int nevent, int *eventidx, const double t, VectorT<T, I>& state)
  {
    if (!p_trigger) {
      p_trigger.reset(new bool[this->size()]);
    }
    for (int i = 0; i < this->size(); ++i) {
      p_trigger[i] = false;
    }
    for (int e = 0; e < nevent; ++e) {
      p_trigger[e] = true;
    }

    T *lstate = state.getLocalElements();

    BOOST_FOREACH(p_EventInfo rec, p_events) {
      for (int i = 0; i < rec.nval; ++i) {
        if (p_trigger[rec.evidx + i]) {
          rec.event->handle(&p_trigger[rec.evidx], t, &lstate[rec.evidx]);
          break;
        }
      }
    }
    state.releaseLocalElements(lstate);
    return;
  }
  
protected:

  /// A thing to hold event information
  struct p_EventInfo {
    EventPtr event;
    int evidx;                  /**< Index of first event value in this record */
    int nval;                   /**< The number of values generated by event */
    I solidx;                   /**< The base index into the (local) solution vector */
  };

  /// All the DAEEventT instances managed by this instance
  std::list<p_EventInfo> p_events;

  /// Cache the overall number of event values
  mutable int p_size;

  /// An array of all event directions
  mutable boost::scoped_array<DAEEventDirection> p_dirs;

  /// An array of all event values
  mutable boost::scoped_array<T> p_values;

  /// An array of trigger flags
  mutable boost::scoped_array<T> p_trigger;
};



} // namespace math
} // namespace gridpack


#endif
