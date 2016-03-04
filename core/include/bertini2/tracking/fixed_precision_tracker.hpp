//This file is part of Bertini 2.0.
//
//amp_tracker.hpp is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.
//
//amp_tracker.hpp is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with amp_tracker.hpp.  If not, see <http://www.gnu.org/licenses/>.
//

//  amp_tracker.hpp
//
//  copyright 2015
//  Daniel Brake
//  University of Notre Dame
//  ACMS
//  Fall 2015

/**
\file amp_tracker.hpp

\brief 

\brief Contains the EmittedType  type, and necessary functions.
*/

#ifndef BERTINI_FIXED_PRECISION_TRACKER_HPP
#define BERTINI_FIXED_PRECISION_TRACKER_HPP

#include "tracking/base_tracker.hpp"


namespace bertini{

	namespace tracking{

		using namespace bertini::tracking::config;
		using std::max;
		using std::min;
		using std::pow;

		using bertini::max;


	




		/** 
		\class FixedPrecisionTracker<prec>

		\brief Functor-like class for tracking paths on a system
		*/
		template<class D>
		class FixedPrecisionTracker : public Tracker<FixedPrecisionTracker<D> >
		{	
		public:

			// typedef typename D::CT ComplexType;
			// typedef typename D::RT RealType;

			using BaseComplexType = typename TrackerTraits<D>::BaseComplexType;
			using BaseRealType = typename TrackerTraits<D>::BaseRealType;

			using CT = BaseComplexType;
			using RT = BaseRealType;

			virtual ~FixedPrecisionTracker() = default;

			typedef FixedPrecisionTracker<D> EmittedType;

			typedef Tracker<FixedPrecisionTracker<D> > Base;

			FixedPrecisionTracker(System const& sys) : Base(sys){}

			/**
			\brief Set up the internals of the tracker for a fresh start.  

			Copies the start time, current stepsize, and start point.  Adjusts the current precision to match the precision of the start point.  Zeros counters.

			\param start_time The time at which to start tracking.
			\param end_time The time to which to track.
			\param start_point The space values from which to start tracking.
			*/
			SuccessCode TrackerLoopInitialization(CT const& start_time,
			                               CT const& end_time,
										   Vec<CT> const& start_point) const override
			{
				this->NotifyObservers(Initializing<FixedPrecisionTracker,CT>(*this,start_time, end_time, start_point));

				// set up the master current time and the current step size
				this->current_time_ = start_time;

				if (this->reinitialize_stepsize_)
					this->SetStepSize(min(this->stepping_config_.initial_step_size,abs(start_time-end_time)/this->stepping_config_.min_num_steps));

				ResetCounters();

				return SuccessCode::Success;
			}


			void ResetCounters() const override
			{
				Base::ResetCounters();

				this->num_successful_steps_since_stepsize_increase_ = 0;
				// initialize to the frequency so guaranteed to compute it the first try 	
				this->num_steps_since_last_condition_number_computation_ = this->frequency_of_CN_estimation_;
			}


			/**
			\brief Ensure that number of steps, stepsize, and precision still ok.

			\return Success if ok to keep going, and a different code otherwise. 
			*/
			SuccessCode PreIterationCheck() const override
			{
				if (this->num_successful_steps_taken_ >= this->stepping_config_.max_num_steps)
					return SuccessCode::MaxNumStepsTaken;
				if (this->current_stepsize_ < this->stepping_config_.min_step_size)
					return SuccessCode::MinStepSizeReached;

				return SuccessCode::Success;
			}






			
			


			void PostTrackCleanup() const override
			{
				this->NotifyObservers(TrackingEnded<EmittedType >(*this));
			}

			/**
			\brief Copy from the internally stored current solution into a final solution.
			
			If preservation of precision is on, this function first returns to the initial precision.

			\param[out] solution_at_endtime The solution at the end time
			*/
			void CopyFinalSolution(Vec<CT> & solution_at_endtime) const override
			{

				// the current precision is the precision of the output solution point.

				unsigned num_vars = this->tracked_system_.NumVariables();
				solution_at_endtime.resize(num_vars);
				for (unsigned ii=0; ii<num_vars; ii++)
				{
					solution_at_endtime(ii) = std::get<Vec<CT> >(this->current_space_)(ii);
				}

			}




			/**
			\brief Run an iteration of the tracker loop.

			Predict and correct, adjusting precision and stepsize as necessary.

			\return Success if the step was successful, and a non-success code if something went wrong, such as a linear algebra failure or AMP Criterion violation.
			*/
			SuccessCode TrackerIteration() const override
			{
				static_assert(std::is_same<	typename Eigen::NumTraits<RT>::Real, 
			              				typename Eigen::NumTraits<CT>::Real>::value,
			              				"underlying complex type and the type for comparisons must match");

				this->NotifyObservers(NewStep<EmittedType >(*this));

				Vec<CT>& predicted_space = std::get<Vec<CT> >(this->temporary_space_); // this will be populated in the Predict step
				Vec<CT>& current_space = std::get<Vec<CT> >(this->current_space_); // the thing we ultimately wish to update
				CT current_time = CT(this->current_time_);
				CT delta_t = CT(this->delta_t_);

				SuccessCode predictor_code = Predict<CT, RT>(predicted_space, current_space, current_time, delta_t);

				if (predictor_code!=SuccessCode::Success)
				{
					this->NotifyObservers(FirstStepPredictorMatrixSolveFailure<EmittedType >(*this));

					this->next_stepsize_ = this->stepping_config_.step_size_fail_factor*this->current_stepsize_;

					UpdateStepsize();

					return predictor_code;
				}

				this->NotifyObservers(SuccessfulPredict<EmittedType , CT>(*this, predicted_space));

				Vec<CT>& tentative_next_space = std::get<Vec<CT> >(this->tentative_space_); // this will be populated in the Correct step

				CT tentative_next_time = current_time + delta_t;

				SuccessCode corrector_code = Correct<CT, RT>(tentative_next_space,
													 predicted_space,
													 tentative_next_time);

				if (corrector_code == SuccessCode::GoingToInfinity)
				{
					// there is no corrective action possible...
					return corrector_code;
				}
				else if (corrector_code!=SuccessCode::Success)
				{
					this->NotifyObservers(CorrectorMatrixSolveFailure<EmittedType >(*this));

					this->next_stepsize_ = this->stepping_config_.step_size_fail_factor*this->current_stepsize_;
					UpdateStepsize();

					return corrector_code;
				}

				
				this->NotifyObservers(SuccessfulCorrect<EmittedType , CT>(*this, tentative_next_space));

				// copy the tentative vector into the current space vector;
				current_space = tentative_next_space;
				return SuccessCode::Success;
			}


			/**
			Check whether the path is going to infinity.
			*/
			SuccessCode CheckGoingToInfinity() const override
			{
				return Base::template CheckGoingToInfinity<CT>();
			}

			



			/**
			\brief Commit the next stepsize, and adjust internals.
			*/
			SuccessCode UpdateStepsize() const
			{
				this->SetStepSize(this->next_stepsize_);
				return SuccessCode::Success;
			}





			///////////
			//
			//  overrides for counter adjustment after a TrackerIteration()
			//
			////////////////

			/**
			\brief Increment and reset counters after a successful TrackerIteration()
			*/
			void IncrementCountersSuccess() const override
			{
				Base::IncrementCountersSuccess();
				this->NotifyObservers(SuccessfulStep<EmittedType >(*this));
			}

			/**
			\brief Increment and reset counters after a failed TrackerIteration()
			*/
			void IncrementCountersFail() const override
			{
				Base::IncrementCountersFail();
				this->num_successful_steps_since_stepsize_increase_ = 0;
				this->NotifyObservers(FailedStep<EmittedType >(*this));
			}



			void OnInfiniteTruncation() const override
			{
				this->NotifyObservers(InfinitePathTruncation<EmittedType >(*this));
			}

			//////////////
			//
			//


			/**
			\brief Wrapper function for calling the correct predictor.
			
			This function computes the next predicted space value, and sets some internals based on the prediction, such as the norm of the Jacobian.

			The real type and complex type must be commensurate.

			\param[out] predicted_space The result of the prediction
			\param current_space The current space point.
			\param current_time The current time value.
			\param delta_t The time differential for this step.  Allowed to be complex.

			\tparam ComplexType The complex number type.
			\tparam RealType The real number type.
			*/
			template<typename ComplexType, typename RealType>
			SuccessCode Predict(Vec<ComplexType> & predicted_space, 
								Vec<ComplexType> const& current_space, 
								ComplexType const& current_time, ComplexType const& delta_t) const
			{
				static_assert(std::is_same<	typename Eigen::NumTraits<RealType>::Real, 
			              				typename Eigen::NumTraits<ComplexType>::Real>::value,
			              				"underlying complex type and the type for comparisons must match");

				RealType& norm_J = std::get<RealType>(this->norm_J_);
				RealType& norm_J_inverse = std::get<RealType>(this->norm_J_inverse_);
				RealType& size_proportion = std::get<RealType>(this->size_proportion_);
				RealType& error_estimate = std::get<RealType>(this->error_estimate_);
				RealType& condition_number_estimate = std::get<RealType>(this->condition_number_estimate_);

				return ::bertini::tracking::Predict(
			                this->predictor_choice_,
							predicted_space,
							this->tracked_system_,
							current_space, current_time,
							delta_t,
							condition_number_estimate,
							this->num_steps_since_last_condition_number_computation_,
							this->frequency_of_CN_estimation_,
							RealType(this->tracking_tolerance_));
			}



			/**
			\brief Run Newton's method.

			Wrapper function for calling Correct and getting the error estimates etc directly into the tracker object.

			\tparam ComplexType The complex number type.
			\tparam RealType The real number type.

			\param corrected_space[out] The spatial result of the correction loop.
			\param current_space The start point in space for running the corrector loop.
			\param current_time The current time value.

			\return A SuccessCode indicating whether the loop was successful in converging in the max number of allowable newton steps, to the current path tolerance.
			*/
			template<typename ComplexType, typename RealType>
			SuccessCode Correct(Vec<ComplexType> & corrected_space, 
								Vec<ComplexType> const& current_space, 
								ComplexType const& current_time) const
			{
				static_assert(std::is_same<	typename Eigen::NumTraits<RealType>::Real, 
			              				typename Eigen::NumTraits<ComplexType>::Real>::value,
			              				"underlying complex type and the type for comparisons must match");


				RealType& norm_J = std::get<RealType>(this->norm_J_);
				RealType& norm_J_inverse = std::get<RealType>(this->norm_J_inverse_);
				RealType& norm_delta_z = std::get<RealType>(this->norm_delta_z_);
				RealType& condition_number_estimate = std::get<RealType>(this->condition_number_estimate_);


				return bertini::tracking::Correct(corrected_space,
												this->tracked_system_,
												current_space,
												current_time, 
												RealType(this->tracking_tolerance_),
												this->newton_config_.min_num_newton_iterations,
												this->newton_config_.max_num_newton_iterations);
			}



			/**
			\brief Run Newton's method from a start point with a current time.  

			Returns new space point by reference, as new_space.  Operates at current precision.  The tolerance is the tracking tolerance specified during Setup(...).

			\tparam ComplexType The complex number type.
			\tparam RealType The real number type.

			\param[out] new_space The result of running the refinement.
			\param start_point The base point for running Newton's method.
			\param current_time The current time value.

			\return Code indicating whether was successful or not.  Regardless, the value of new_space is overwritten with the correction result.
			*/
			template <typename ComplexType, typename RealType>
			SuccessCode Refine(Vec<ComplexType> & new_space,
								Vec<ComplexType> const& start_point, ComplexType const& current_time) const
			{
				static_assert(std::is_same<	typename Eigen::NumTraits<RealType>::Real, 
			              				typename Eigen::NumTraits<ComplexType>::Real>::value,
			              				"underlying complex type and the type for comparisons must match");

				return bertini::tracking::Correct(new_space,
							   this->tracked_system_,
							   start_point,
							   current_time, 
							   this->tracking_tolerance_,
							   this->newton_config_.min_num_newton_iterations,
							   this->newton_config_.max_num_newton_iterations);
			}






			/**
			\brief Run Newton's method from a start point with a current time.  

			Returns new space point by reference, as new_space.  Operates at current precision.

			\tparam ComplexType The complex number type.
			\tparam RealType The real number type.

			\param[out] new_space The result of running the refinement.
			\param start_point The base point for running Newton's method.
			\param current_time The current time value.
			\param tolerance The tolerance for convergence.  This is a tolerance on \f$\Delta x\f$, not on function residuals.

			\return Code indicating whether was successful or not.  Regardless, the value of new_space is overwritten with the correction result.
			*/
			template <typename ComplexType, typename RealType>
			SuccessCode Refine(Vec<ComplexType> & new_space,
								Vec<ComplexType> const& start_point, ComplexType const& current_time,
								RealType const& tolerance) const
			{
				static_assert(std::is_same<	typename Eigen::NumTraits<RealType>::Real, 
			              				typename Eigen::NumTraits<ComplexType>::Real>::value,
			              				"underlying complex type and the type for comparisons must match");

				return bertini::tracking::Correct(new_space,
							   this->tracked_system_,
							   start_point,
							   current_time, 
							   tolerance,
							   this->newton_config_.min_num_newton_iterations,
							   this->newton_config_.max_num_newton_iterations);
			}

			/////////////////
			//
			//  Functions for refining a current space value
			//
			///////////////////////


			// /**
			// \brief Run Newton's method from the currently stored time and space value, in current precision.

			// This overwrites the value of the current space, if successful, with the refined value.

			// \return Whether the refinement was successful.
			// */
			// SuccessCode Refine() const
			// {
			// 	SuccessCode code;
			// 	if (current_precision_==DoublePrecision())
			// 	{
			// 		code = Refine(std::get<Vec<dbl> >(temporary_space_),std::get<Vec<dbl> >(current_space_), dbl(current_time_),double(tracking_tolerance_));
			// 		if (code == SuccessCode::Success)
			// 			std::get<Vec<dbl> >(current_space_) = std::get<Vec<dbl> >(temporary_space_);
			// 	}
			// 	else
			// 	{
			// 		code = Refine(std::get<Vec<mpfr> >(temporary_space_),std::get<Vec<mpfr> >(current_space_), current_time_, tracking_tolerance_);
			// 		if (code == SuccessCode::Success)
			// 			std::get<Vec<mpfr> >(current_space_) = std::get<Vec<mpfr> >(temporary_space_);
			// 	}
			// 	return code;
			// }



			/////////////////////////////////////////////
			//////////////////////////////////////
			/////////////////////////////
			////////////////////  data members stored in this class
			////////////
			//////
			//

			// no additional state variables needed for the FixedPrecision base tracker types

		};




		class DoublePrecisionTracker : public FixedPrecisionTracker<DoublePrecisionTracker>
		{
		public:
			using BaseComplexType = dbl;
			using BaseRealType = double;

			BERTINI_DEFAULT_VISITABLE()

			/**
			\brief Construct an Adaptive Precision tracker, associating to it a System.
			*/
			DoublePrecisionTracker(class System const& sys) : FixedPrecisionTracker<DoublePrecisionTracker>(sys)
			{	}


			DoublePrecisionTracker() = delete;

			virtual ~DoublePrecisionTracker() = default;


			unsigned CurrentPrecision() const override
			{
				return DoublePrecision();
			}

		private:

		}; // re: class Tracker

	} // namespace tracking
} // namespace bertini


#endif


