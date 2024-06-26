/*

Copyright (c) 2005-2016, University of Oxford.
 All rights reserved.

 University of Oxford means the Chancellor, Masters and Scholars of the
 University of Oxford, having an administrative office at Wellington
 Square, Oxford OX1 2JD, UK.

 This file is part of Chaste.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name of the University of Oxford nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 */

#include <math.h>
#include "SimpleLinearEllipticSolver.hpp"
#include "SimpleNonlinearEllipticSolver.hpp"
#include "SimpleNewtonNonlinearSolver.hpp"
#include "FiniteElementSolver.hpp"

template<unsigned DIM>
FiniteElementSolver<DIM>::FiniteElementSolver()
    : AbstractUnstructuredGridDiscreteContinuumSolver<DIM>(),
      mUseNewton(false),
      mUseLinearSolveForGuess(false),
      mGuess()
{

}

template<unsigned DIM>
FiniteElementSolver<DIM>::~FiniteElementSolver()
{

}

template <unsigned DIM>
boost::shared_ptr<FiniteElementSolver<DIM> > FiniteElementSolver<DIM>::Create()
{
    MAKE_PTR(FiniteElementSolver<DIM>, pSelf);
    return pSelf;
}

template<unsigned DIM>
void FiniteElementSolver<DIM>::Update()
{
    if(this->mpPde)
    {
        this->mpPde->UpdateDiscreteSourceStrengths();
    }
    else
    {
        this->mpNonLinearPde->UpdateDiscreteSourceStrengths();
    }
}

template<unsigned DIM>
void FiniteElementSolver<DIM>::SetGuess(const std::vector<double>& guess)
{
    mGuess = guess;
}

template<unsigned DIM>
void FiniteElementSolver<DIM>::SetUseSimpleNetonSolver(bool useNewton)
{
    mUseNewton = useNewton;
}

template<unsigned DIM>
void FiniteElementSolver<DIM>::SetUseLinearSolveForGuess(bool useLinearSolve)
{
    mUseLinearSolveForGuess = useLinearSolve;
}

template<unsigned DIM>
void FiniteElementSolver<DIM>::Solve()
{
    if(!this->IsSetupForSolve)
    {
        this->Setup();
    }

    // Set up the boundary conditions in the Chaste format
    boost::shared_ptr<BoundaryConditionsContainer<DIM, DIM, 1> > p_bcc =
            boost::shared_ptr<BoundaryConditionsContainer<DIM, DIM, 1> >(new BoundaryConditionsContainer<DIM, DIM, 1> );

    for(unsigned idx=0; idx<this->mBoundaryConditions.size(); idx++)
    {
        this->mBoundaryConditions[idx]->SetMesh(this->mpMesh);
        this->mBoundaryConditions[idx]->UpdateBoundaryConditionContainer(p_bcc);
    }

    // Do the solve
    // Check the type of pde
    if(this->mpPde and !this->mpNonLinearPde)
    {
        this->mpPde->SetUseRegularGrid(false);
        this->mpPde->SetMesh(this->mpMesh);
        this->mpPde->UpdateDiscreteSourceStrengths();

        SimpleLinearEllipticSolver<DIM, DIM> static_solver(this->mpMesh.get(), this->mpPde.get(), p_bcc.get());
        ReplicatableVector solution_repl(static_solver.Solve());

        this->mSolution = std::vector<double>(solution_repl.GetSize());
        this->mConcentrations = std::vector<units::quantity<unit::concentration> >(solution_repl.GetSize());
        for(unsigned idx = 0; idx < solution_repl.GetSize(); idx++)
        {
            this->mSolution[idx] = solution_repl[idx];
            this->mConcentrations[idx] = solution_repl[idx]*this->mReferenceConcentration;
        }
        this->UpdateSolution(this->mSolution);
    }
    else if(this->mpNonLinearPde)
    {
        this->mpNonLinearPde->SetUseRegularGrid(false);
        this->mpNonLinearPde->SetMesh(this->mpMesh);
        this->mpNonLinearPde->UpdateDiscreteSourceStrengths();

        if (this->mpPde)
        {
            this->mpPde->SetUseRegularGrid(false);
            this->mpPde->SetMesh(this->mpMesh);
            this->mpPde->UpdateDiscreteSourceStrengths();

            SimpleLinearEllipticSolver<DIM, DIM> static_solver(this->mpMesh.get(), this->mpPde.get(), p_bcc.get());
            ReplicatableVector static_solution_repl(static_solver.Solve());

            std::vector<double> solution = std::vector<double>(static_solution_repl.GetSize());
            for(unsigned idx = 0; idx < static_solution_repl.GetSize(); idx++)
            {
                solution[idx]= static_solution_repl[idx];
                // Dont want negative solutions going into the initial guess
                if(solution[idx]<0.0)
                {
                    solution[idx] = 0.0;
                }
            }

            Vec initial_guess = PetscTools::CreateVec(this->mpMesh->GetNumNodes());
            for(unsigned idx=0; idx<solution.size();idx++)
            {
                PetscVecTools::SetElement(initial_guess, idx, solution[idx]);
            }
            PetscVecTools::Finalise(initial_guess);

            SimpleNonlinearEllipticSolver<DIM, DIM> solver(this->mpMesh.get(), this->mpNonLinearPde.get(), p_bcc.get());
            SimpleNewtonNonlinearSolver newton_solver;
            if(mUseNewton)
            {
                solver.SetNonlinearSolver(&newton_solver);
                newton_solver.SetTolerance(1e-5);
                newton_solver.SetWriteStats();
            }

            ReplicatableVector solution_repl(solver.Solve(initial_guess));
            this->mSolution = std::vector<double>(solution_repl.GetSize());
            this->mConcentrations = std::vector<units::quantity<unit::concentration> >(solution_repl.GetSize());
            for(unsigned idx = 0; idx < solution_repl.GetSize(); idx++)
            {
                this->mSolution[idx] = solution_repl[idx];
                this->mConcentrations[idx] = solution_repl[idx]*this->mReferenceConcentration;
            }
            this->UpdateSolution(this->mSolution);
            PetscTools::Destroy(initial_guess);
        }
        else
        {
            Vec initial_guess = PetscTools::CreateAndSetVec(this->mpMesh->GetNumNodes(), this->mBoundaryConditions[0]->GetValue()/this->mReferenceConcentration);
            SimpleNonlinearEllipticSolver<DIM, DIM> solver(this->mpMesh.get(), this->mpNonLinearPde.get(), p_bcc.get());
            SimpleNewtonNonlinearSolver newton_solver;
            if(mUseNewton)
            {
                solver.SetNonlinearSolver(&newton_solver);
                newton_solver.SetTolerance(1e-5);
                newton_solver.SetWriteStats();
            }

            ReplicatableVector solution_repl(solver.Solve(initial_guess));
            this->mSolution = std::vector<double>(solution_repl.GetSize());
            this->mConcentrations = std::vector<units::quantity<unit::concentration> >(solution_repl.GetSize());
            for(unsigned idx = 0; idx < solution_repl.GetSize(); idx++)
            {
                this->mSolution[idx] = solution_repl[idx];
                this->mConcentrations[idx] = solution_repl[idx]*this->mReferenceConcentration;
            }
            this->UpdateSolution(this->mSolution);
            PetscTools::Destroy(initial_guess);
        }
    }
    else
    {
        EXCEPTION("PDE Type could not be identified, did you set a PDE?");
    }

    if(this->mWriteSolution)
    {
        this->Write();
    }
}

// Explicit instantiation
template class FiniteElementSolver<2> ;
template class FiniteElementSolver<3> ;
