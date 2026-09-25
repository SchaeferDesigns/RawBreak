#include "rb/Core/FpGuard.h"
// Owner: WP-2 (equipment & table geometry). Spec: equipment 2, 4, 5, 11 (ValidateWpa sketch).
#include "rb/Equipment/TableSpec.h"

namespace rb
{
	WpaReport ValidateWpa(const TableSpec& /*Spec*/)
	{
		// TODO(WP-2): per-field WPA range checks (T-WPA-1..3); returns an all-pass report until implemented.
		return {};
	}
}
