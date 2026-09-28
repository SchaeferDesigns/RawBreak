#pragma once

// Shared automation-test conventions (Docs/ue-architecture.md 10):
//   RawBreak.Unit.<Area>.<Name>        pure logic, run under -NullRHI in the editor process (rbue.py test)
//   RawBreak.Functional.<Name>         needs a world (PIE / game) - latent commands, may need -RenderOffscreen
//   RawBreak.Screenshot.<Name>         capture + comparison (look-dev regression, plan E2)
// Test ids of the specs go into the test name (e.g. ...Coords.T14_Position) so traceability greps work.

#include "Misc/AutomationTest.h"

#define RB_UNIT_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)
