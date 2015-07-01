#include "function_tree/operators/arithmetic.hpp"


namespace bertini{

	using ::pow;

	///////////////////////
	//
	//  Sum Operator definitions
	//
	//////////////////////

	void SumOperator::print(std::ostream & target) const
		{
			target << "(";
			for (auto iter = children_.begin(); iter!= children_.end(); iter++) {
				if (iter==children_.begin()) {
					// on the first iteration, no need to put a + if a +
					if ( !(*(children_sign_.begin()+(iter-children_.begin()))) )
						target << "-";
				}
				else
				{
					if ( !(*(children_sign_.begin()+(iter-children_.begin()))) )
						target << "-";
					else
						target << "+";
				}
				(*iter)->print(target);
				
			}
			target << ")";
		}

	std::shared_ptr<Node> SumOperator::Differentiate()
        {
        	unsigned int counter = 0;
            std::shared_ptr<SumOperator> ret_sum = std::make_shared<SumOperator>();
            for (int ii = 0; ii < children_.size(); ++ii)
            {
            	auto converted = std::dynamic_pointer_cast<Number>(children_[ii]);
            	if (converted)
            		continue;
            	
            	auto temp_node = children_[ii]->Differentiate();
				converted = std::dynamic_pointer_cast<Number>(temp_node);
				if (converted)
					if (converted->Eval<dbl>()==dbl(0.0))
						continue;

                ret_sum->AddChild(temp_node,children_sign_[ii]);
                counter++;
            }
            
            if (counter>0)
            	return ret_sum;
            else
            	return std::make_shared<Number>(0.0);
        }




    int SumOperator::Degree(std::shared_ptr<Variable> const& v) const
		{
			int deg = 0;

			for (auto iter: children_)
			{
				auto curr_deg = iter->Degree(v);
				if (curr_deg<0)
				    return curr_deg;

				deg = std::max(deg, curr_deg);
			}
			return deg;
		}


	void SumOperator::Homogenize(std::vector< std::shared_ptr< Variable > > const& vars, std::shared_ptr<Variable> const& homvar)
	{
		
	
	
		// first homogenize each summand.
		for (auto iter: children_)
		{
			iter->Homogenize(vars, homvar);
		}

		// then, homogenize this sum.

		// compute the highest degree among all summands.
		int maxdegree = 0;
		std::vector<int> term_degrees;
		// first homogenize each summand.
		for (auto iter: children_)
		{
			auto local_degree = iter->Degree(vars);
			if (local_degree<0)
				throw std::runtime_error("asking for homogenization on non-polynomial node");
				// TODO: this throw would leave the tree in a partially homogenized state.  this is scary.

			term_degrees.push_back(local_degree);
			maxdegree = std::max(maxdegree, local_degree);
		}

		for (auto iter = children_.begin(); iter!=children_.end(); iter++)
		{
			auto degree_deficiency = maxdegree - *(term_degrees.begin() + (iter-children_.begin()));
			if ( degree_deficiency > 0)
			{
				// hold the child temporarily.
				std::shared_ptr<Node> P = std::make_shared<IntegerPowerOperator>(std::dynamic_pointer_cast<Node>(homvar),degree_deficiency);
				std::shared_ptr<Node> M = std::make_shared<MultOperator>(P,std::dynamic_pointer_cast<Node>(*iter));
				
				
				swap(*iter,M);
			}
		}

	}

	dbl SumOperator::FreshEval(dbl, std::shared_ptr<Variable> diff_variable)
        {
            dbl retval{0};
            for(int ii = 0; ii < children_.size(); ++ii)
            {
                if(children_sign_[ii])
                {
                    retval += children_[ii]->Eval<dbl>(diff_variable);
                }
                else
                {
                    retval -= children_[ii]->Eval<dbl>(diff_variable);
                }
            }
            
            return retval;
        }

    mpfr SumOperator::FreshEval(mpfr, std::shared_ptr<Variable> diff_variable)
        {
            mpfr retval{0};
            for(int ii = 0; ii < children_.size(); ++ii)
            {
                if(children_sign_[ii])
                {
                    retval += children_[ii]->Eval<mpfr>(diff_variable);
                }
                else
                {
                    retval -= children_[ii]->Eval<mpfr>(diff_variable);
                }
            }
            
            return retval;
        }



	///////////////////////
	//
	//  Mult Operator definitions
	//
	//////////////////////

	std::shared_ptr<Node> MultOperator::Differentiate()
        {
            std::shared_ptr<SumOperator> ret_sum = std::make_shared<SumOperator>();

            unsigned term_counter {0};

            for (int ii = 0; ii < children_.size(); ++ii)
            {

            	auto local_derivative = children_[ii]->Differentiate();

                auto is_it_a_number = std::dynamic_pointer_cast<Number>(local_derivative);
                if (is_it_a_number)
                	if (is_it_a_number->Eval<dbl>()==dbl(0.0))
                		continue;



                auto term_ii = std::make_shared<MultOperator>();
                for (int jj = 0; jj < children_.size(); ++jj)
                {
                    if(jj != ii)
                        term_ii->AddChild(children_[jj],children_mult_or_div_[jj]);
                }


                // if the derivative of the term under consideration is equal to 1, no need to go on.  already have the product we need.
                if (is_it_a_number)
                	if (is_it_a_number->Eval<dbl>()==dbl(1.0))
                		continue;
                	

                // the first branch is for division, and the quotient rule.
                if ( !(children_mult_or_div_[ii]) )
                {
                	// divide by the 
                    term_ii->AddChild(children_[ii],false);
                    term_ii->AddChild(children_[ii],false);
                    term_ii->AddChild(local_derivative);
                    ret_sum->AddChild(term_ii,false);  // false for subtract here.
                }
                // the second branch is for multiplication, and the product rule
                else
                {
                    term_ii->AddChild(local_derivative,true);// true for multiply, not divide
                    ret_sum->AddChild(term_ii); //  no truth second argument, because is add.
                }

                term_counter++;

            } // re: for ii

            return ret_sum;
        }








        /////////////////
        //
        //  IntegerPowerOperator definitions
        //
        ////////////////////

    /**
	 Virtual polymorphic method for printing to an arbitrary stream.
	 */
	 void IntegerPowerOperator::print(std::ostream & target) const
	 {
	 	target << "(";
	 	            child_->print(target);
	 	            target << "^" << exponent() << ")";
	}


	std::shared_ptr<Node> IntegerPowerOperator::Differentiate()
	{

		if (exponent_==0)
			return std::make_shared<Number>(0.0);
		else if (exponent_==1.0)
			return child_->Differentiate();
		else if (exponent_==2){
			auto M = std::make_shared<MultOperator>(std::make_shared<Number>(2.0), child_);
			M->AddChild(child_->Differentiate());
			return M;
		}
		else{
			auto M = std::make_shared<MultOperator>(std::make_shared<Number>(exponent_), 
			                                        std::make_shared<IntegerPowerOperator>(child_, exponent_-1) );
			M->AddChild(child_->Differentiate());
			return M;
		}
	}

	int IntegerPowerOperator::Degree(std::shared_ptr<Variable> const& v) const
	{
		auto base_deg = child_->Degree(v);
		if (base_deg<0)
			return base_deg;
		else
			return exponent_*base_deg;

	}



}
