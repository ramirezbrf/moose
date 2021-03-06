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

#ifndef OUTPUTINTERFACE_H
#define OUTPUTINTERFACE_H

// MOOSE includes
#include "InputParameters.h"

// Forward declerations
class OutputInterface;
class OutputWarehouse;

template<>
InputParameters validParams<OutputInterface>();

/**
 * A class to provide an common interface to objects requiring "outputs" option
 *
 * The 'outputs' option, when set restricts the output of the variable(s) associated with
 * this object to only occur on output objects listed.
 */
class OutputInterface
{
public:

  /**
   * Handles 'outputs' parameter for objects that desire control of variable outputs
   * @param name Name of the object; also the assumed default name for the output variable to limit
   * @param parameters The input parameters for the object
   * @param build_list If false the buildOutputHideVariableList must be called explicitly, this behavior
   *                   is required for automatic output of material properties
   */
  OutputInterface(const std::string & name, InputParameters parameters, bool build_list = true);

  /**
   * Builds hide lists for output objects NOT listed in the 'outputs' parameter
   * @param variable_names A set of variables for which the 'outputs' parameter controls
   *
   * By default this is called by the constructor and passes the block name as the list of
   * variables. This needs to be called explicitly if the build_list flag is set to False
   * in the constructor. The latter cases is needed by the Material object to work correctly
   * with the automatic material output capability.
   */
  void buildOutputHideVariableList(std::set<std::string> variable_names);

  /**
   * Get the list of output objects that this class is restricted
   * @return A set of OutputNames
   */
  const std::set<OutputName> & getOutputs();

private:

  /// Reference the the MooseApp; neede for access to the OutputWarehouse
  MooseApp & _oi_moose_app;

  /// Reference to the OutputWarehouse for populating the Output object hide lists
  OutputWarehouse & _oi_output_warehouse;

  /// The set of Output object names listed in the 'outputs' parameter
  std::set<OutputName> _oi_outputs;
};

#endif // OUTPUTINTERFACE_H
