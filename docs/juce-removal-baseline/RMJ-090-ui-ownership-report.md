# RMJ-090 theme and popup ownership contracts

The neutral UI layer now defines semantic theme tokens with a shared fixed-dark
spectrum canvas across Ember and Porcelain, plus a single-owner popup manager
with modal/outside-dismissal policy, stable IDs, and focus restoration metadata.
The contract test covers theme separation, popup replacement state, dismissal,
and modal outside-click protection. Native drawing and real-dispatch behavior
remain backend-dependent.
