# Actual Metalio portrait wrist trace

Source: controlled user action on 2026-10-28 at 23:14:32 Asia/Shanghai,
left tilt / hold / return / right tilt / hold / return, three seconds per hold.
The user reported missed turns or a stuck detector on the old mounting map.

1113 fresh samples. Columns (no header):
`reset,device_ms,chip_x_mg,chip_y_mg,chip_z_mg,landscape,sensitivity,touch_blocked`.
Coordinates are physical SC7A20H coordinates before any mounting transform.
The fixture is extracted directly from the full-sample serial capture; it is
not an interpolated reconstruction of sparse logs.

With the Metalio mount rotation `(Y,-X,Z)`, expected raw intents are -1 at
4565ms and +1 at 10868ms after the first sample. No intent on return or idle.
Host reproduction does not establish post-flash physical acceptance.
