# Does a sideways tip velocity (swoop) at contact change the miscue (friction) demand?
# Grip model with the cue's axial mass M and the shaft END MASS m_e transverse (TP A.31 / MOT B.7 assumption).
import numpy as np
m, R = 0.170, 0.028575
I = 0.4 * m * R * R
M = 0.5386409
e = 0.73

def demand(a, b, V, vr, vu, me):
    er = np.array([1.0, 0, 0]); d = np.array([0, 1.0, 0]); eu = np.array([0, 0, 1.0])  # any right-handed triad
    c = np.sqrt(1 - a * a - b * b)
    Q = R * (a * er + b * eu - c * d)
    n = -Q / R
    def Kball(P):
        dw = np.cross(Q, P) / I
        return P / m + np.cross(dw, Q)
    def Kcue(P):
        Pa = np.dot(P, d) * d
        Pt = P - Pa
        return Pa / M + (Pt / me if me < 1e9 else 0 * Pt)
    Kmat = np.zeros((3, 3))
    for i in range(3):
        E = np.zeros(3); E[i] = 1
        Kmat[:, i] = Kball(E) + Kcue(E)
    u0 = V * d + vr * er + vu * eu
    u1 = -e * np.dot(u0, n) * n            # stick: tangential relative velocity 0, normal restitution
    P = np.linalg.solve(Kmat, u0 - u1)
    Pn = np.dot(P, n)
    Pt = P - Pn * n
    return np.linalg.norm(Pt) / Pn, P

def hf_rhoeff(a, b, V, vr, vu):
    c = np.sqrt(1 - a * a - b * b)
    cp = (-a * vr + c * V - b * vu) / np.sqrt(V * V + vr * vr + vu * vu)
    return np.sqrt(1 - cp * cp)

for me in [0.170 / 15, 0.0085, 0.170 / 40, 1e12]:
    base, P0 = demand(0.48, 0, 1.0, 0.0, 0.0, me)
    plus, _ = demand(0.48, 0, 1.0, 0.05, 0.0, me)
    minus, _ = demand(0.48, 0, 1.0, -0.05, 0.0, me)
    print(f"m_e={me:.4g}: demand tan(angle) 0 / +5cm/s / -5cm/s = {base:.5f} {plus:.5f} {minus:.5f}; "
          f"equivalent rho = {base/np.sqrt(1+base**2):.5f} {plus/np.sqrt(1+plus**2):.5f} {minus/np.sqrt(1+minus**2):.5f}")
print("MOT criterion tan(psi) at a=0.48:", 0.48 / np.sqrt(1 - 0.48 ** 2))
print("HF RhoEffective:", hf_rhoeff(0.48, 0, 1, 0, 0), hf_rhoeff(0.48, 0, 1, 0.05, 0), hf_rhoeff(0.48, 0, 1, -0.05, 0))
