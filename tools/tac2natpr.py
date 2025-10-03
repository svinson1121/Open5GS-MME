#!/usr/bin/python3
def tac_lb_hb(tac):
    """
    Convert TAC to 3GPP DNS tac-lb / tac-hb format.
    """
    if not (0 <= tac <= 0xFFFF):
        raise ValueError("TAC must be between 0 and 65535")

    lb = tac & 0xFF         # low byte
    hb = (tac >> 8) & 0xFF  # high byte
    return f"tac-lb{lb}.tac-hb{hb}"


if __name__ == "__main__":
    try:
        tac_input = int(input("Enter TAC (0–65535): "))
        result = tac_lb_hb(tac_input)
        print(f"Result: {result}")
    except ValueError as e:
        print(f"Error: {e}")

