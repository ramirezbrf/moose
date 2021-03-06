/****************************************************************/
/*               DO NOT MODIFY THIS HEADER                      */
/* MOOSE - Multiphysics Object Oriented Simulation Environment  */
/*                                                              */
/*           (c) 2010 Battelle Energy Alliance, LLC             */
/*                   ALL RIGHTS RESERVED                        */
/*                                                              */
/*          Prepared by Battelle Energy Alliance, LLC           */
/*            Under Contract No. DE-AC07-05ID14517              */
/*            With the U. S. Department of Energy               */
/*                                                              */
/*            See COPYRIGHT for full restrictions               */
/****************************************************************/

#include "ComputeJacobianThread.h"
#include "NonlinearSystem.h"
#include "FEProblem.h"
#include "TimeDerivative.h"
#include "IntegratedBC.h"
#include "DGKernel.h"

// libmesh includes
#include "libmesh/threads.h"

ComputeJacobianThread::ComputeJacobianThread(FEProblem & fe_problem, NonlinearSystem & sys, SparseMatrix<Number> & jacobian) :
    ThreadedElementLoop<ConstElemRange>(fe_problem, sys),
    _jacobian(jacobian),
    _sys(sys),
    _num_cached(0)
{
}

// Splitting Constructor
ComputeJacobianThread::ComputeJacobianThread(ComputeJacobianThread & x, Threads::split split) :
    ThreadedElementLoop<ConstElemRange>(x, split),
    _jacobian(x._jacobian),
    _sys(x._sys),
    _num_cached(x._num_cached)
{
}

ComputeJacobianThread::~ComputeJacobianThread()
{
}

void
ComputeJacobianThread::computeJacobian()
{
  const std::vector<KernelBase *> & kernels = _sys.getKernelWarehouse(_tid).active();
  for (std::vector<KernelBase *>::const_iterator it = kernels.begin(); it != kernels.end(); ++it)
  {
    KernelBase * kernel = *it;
    if (kernel->isImplicit())
    {
      kernel->subProblem().prepareShapes(kernel->variable().number(), _tid);
      kernel->computeJacobian();
    }
  }
}

void
ComputeJacobianThread::computeFaceJacobian(BoundaryID bnd_id)
{
  std::vector<IntegratedBC *> bcs;
  _sys.getBCWarehouse(_tid).activeIntegrated(bnd_id, bcs);
  for (std::vector<IntegratedBC *>::iterator it = bcs.begin(); it != bcs.end(); ++it)
  {
    IntegratedBC * bc = *it;
    if (bc->shouldApply() && bc->isImplicit())
    {
      bc->subProblem().prepareFaceShapes(bc->variable().number(), _tid);
      bc->computeJacobian();
    }
  }
}

void
ComputeJacobianThread::computeInternalFaceJacobian()
{
  std::vector<DGKernel *> dgks = _sys.getDGKernelWarehouse(_tid).active();
  for (std::vector<DGKernel *>::iterator it = dgks.begin(); it != dgks.end(); ++it)
  {
    DGKernel * dg = *it;
    if (dg->isImplicit())
    {
      dg->subProblem().prepareFaceShapes(dg->variable().number(), _tid);
      dg->subProblem().prepareNeighborShapes(dg->variable().number(), _tid);
      dg->computeJacobian();
    }
  }
}


void
ComputeJacobianThread::subdomainChanged()
{
  _fe_problem.subdomainSetup(_subdomain, _tid);
  _sys.updateActiveKernels(_subdomain, _tid);
  if (_sys.doingDG())
    _sys.updateActiveDGKernels(_fe_problem.time(), _fe_problem.dt(), _tid);

  std::set<MooseVariable *> needed_moose_vars;

  const std::vector<KernelBase *> & kernels = _sys.getKernelWarehouse(_tid).active();
  for (std::vector<KernelBase *>::const_iterator it = kernels.begin(); it != kernels.end(); ++it)
  {
    const std::set<MooseVariable *> & mv_deps = (*it)->getMooseVariableDependencies();
    needed_moose_vars.insert(mv_deps.begin(), mv_deps.end());
  }

  // Boundary Condition Dependencies
  const std::set<unsigned int> & subdomain_boundary_ids = _mesh.getSubdomainBoundaryIds(_subdomain);
  for (std::set<unsigned int>::const_iterator id_it = subdomain_boundary_ids.begin();
      id_it != subdomain_boundary_ids.end();
      ++id_it)
  {
    std::vector<IntegratedBC *> bcs;
    _sys.getBCWarehouse(_tid).activeIntegrated(*id_it, bcs);
    if (bcs.size() > 0)
    {
      for (std::vector<IntegratedBC *>::iterator it = bcs.begin(); it != bcs.end(); ++it)
      {
        IntegratedBC * bc = (*it);
        if (bc->shouldApply())
        {
          const std::set<MooseVariable *> & mv_deps = bc->getMooseVariableDependencies();
          needed_moose_vars.insert(mv_deps.begin(), mv_deps.end());
        }
      }
    }
  }

  // DG Kernel dependencies
  {
    std::vector<DGKernel *> dgks = _sys.getDGKernelWarehouse(_tid).active();
    for (std::vector<DGKernel *>::iterator it = dgks.begin(); it != dgks.end(); ++it)
    {
      const std::set<MooseVariable *> & mv_deps = (*it)->getMooseVariableDependencies();
      needed_moose_vars.insert(mv_deps.begin(), mv_deps.end());
    }
  }

  _fe_problem.setActiveElementalMooseVariables(needed_moose_vars, _tid);
  _fe_problem.prepareMaterials(_subdomain, _tid);
}

void
ComputeJacobianThread::onElement(const Elem *elem)
{
  _fe_problem.prepare(elem, _tid);

  _fe_problem.reinitElem(elem, _tid);

  _fe_problem.reinitMaterials(_subdomain, _tid);
  if (_sys.getScalarVariables(_tid).size() > 0)
    _fe_problem.reinitOffDiagScalars(_tid);

  computeJacobian();

  _fe_problem.swapBackMaterials(_tid);
}

void
ComputeJacobianThread::onBoundary(const Elem *elem, unsigned int side, BoundaryID bnd_id)
{

  std::vector<IntegratedBC *> bcs;
  _sys.getBCWarehouse(_tid).activeIntegrated(bnd_id, bcs);
  if (bcs.size() > 0)
  {
    _fe_problem.reinitElemFace(elem, side, bnd_id, _tid);

    if (_subdomain != _old_subdomain)
      _fe_problem.subdomainSetupSide(_subdomain, _tid);

    _fe_problem.reinitMaterialsFace(elem->subdomain_id(), _tid);
    _fe_problem.reinitMaterialsBoundary(bnd_id, _tid);

    // Set the active boundary id so that BoundaryRestrictable::_boundary_id is correct
    _fe_problem.setCurrentBoundaryID(bnd_id);

    computeFaceJacobian(bnd_id);

    // Set the active boundary to invalid
    _fe_problem.setCurrentBoundaryID(Moose::INVALID_BOUNDARY_ID);

    _fe_problem.swapBackMaterialsFace(_tid);
  }
}

void
ComputeJacobianThread::onInternalSide(const Elem *elem, unsigned int side)
{
  if (_sys.getDGKernelWarehouse(_tid).active().empty())
    return;

  // Pointer to the neighbor we are currently working on.
  const Elem * neighbor = elem->neighbor(side);

  // Get the global id of the element and the neighbor
  const unsigned int elem_id = elem->id();
  const unsigned int neighbor_id = neighbor->id();

  if ((neighbor->active() && (neighbor->level() == elem->level()) && (elem_id < neighbor_id)) || (neighbor->level() < elem->level()))
  {
    _fe_problem.reinitNeighbor(elem, side, _tid);

    _fe_problem.reinitMaterialsFace(elem->subdomain_id(), _tid);
    _fe_problem.reinitMaterialsNeighbor(neighbor->subdomain_id(), _tid);

    computeInternalFaceJacobian();

    _fe_problem.swapBackMaterialsFace(_tid);
    _fe_problem.swapBackMaterialsNeighbor(_tid);

    {
      Threads::spin_mutex::scoped_lock lock(Threads::spin_mtx);
      _fe_problem.addJacobianNeighbor(_jacobian, _tid);
    }
  }
}

void
ComputeJacobianThread::postElement(const Elem * /*elem*/)
{
  _fe_problem.cacheJacobian(_tid);
  _num_cached++;

  if (_num_cached % 20 == 0)
  {
    Threads::spin_mutex::scoped_lock lock(Threads::spin_mtx);
    _fe_problem.addCachedJacobian(_jacobian, _tid);
  }
}

void
ComputeJacobianThread::post()
{
  _fe_problem.clearActiveElementalMooseVariables(_tid);
}

void ComputeJacobianThread::join(const ComputeJacobianThread & /*y*/)
{
}
