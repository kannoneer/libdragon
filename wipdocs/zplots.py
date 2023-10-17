import matplotlib.pyplot as plt
import numpy as np

near = 1.0
far = 50.0

if False:
    def z_from_d(d):
        a = far/(far-near)
        b = -(far*near)/(far-near)
        # d=(a+b*z)/-z
        # d=a/-z + (b*z)/-z
        # d=-a/z + -(b*z)/z
        # d=-a/z + -(b*1)
        # d=-a/z + -b
        # -d=a/z + b
        # -d - b=a/z
        # 1/(-d - b)=z/a
        # a/(-d - b)=z
        return a/(-d - b)


    def d_from_z(z):
        a = (far*near)/(far-near)
        b = -near/(far-near)
        return (a+b*z)/z
        # return (a/z)+b

if False:
    P_gl = np.mat([[-(far+near)/(far-near), -2*far*near/(far-near)],
                [-1,               0]])
    P_gl_inv = np.linalg.inv(P_gl)

    def d_from_z(z):
        # P = np.mat([[far/(far-near), -(far*near)/(far-near)],
        #             [1,               0]])
        clip = P_gl @ np.array([[z], [1]])

        NDC = clip/clip[1,0]
        return NDC[0,0]

    def z_from_d(d):
        ndc = np.array([[d], [1]])
        not_clip = P_gl_inv @ ndc # this is a weird operation
        clip = not_clip / not_clip[1,0]
        return clip[0,0]

else:
    # reversed post-projection Z by swapping near and far in the projection matrix formula
    P_gl_rev = np.mat([[-(near+far)/(near-far), -2*near*far/(near-far)],
                [-1,               0]])

    P_gl_rev_inv = np.linalg.inv(P_gl_rev)

    def d_from_z(z):
        clip = P_gl_rev @ np.array([[z], [1]])

        NDC = clip/clip[1,0]
        return NDC[0,0]

    def z_from_d(d):
        ndc = np.array([[d], [1]])
        not_clip = P_gl_rev_inv @ ndc # this is a weird operation
        clip = not_clip / not_clip[1,0]
        return clip[0,0]

d_from_zv = np.vectorize(d_from_z)
z_from_dv = np.vectorize(z_from_d)


bits = 8
dtest = d_from_z(20)
print(z_from_d(d_from_z(20)))
print(d_from_z(z_from_d(0.5)))

ds = np.linspace(-1.0, 1.0, num=2**bits, endpoint=False)
zs = np.linspace(-near, -far, num=100, endpoint=True)

fig, ax = plt.subplots(1,1)
ax.scatter(-z_from_dv(ds), ds, marker='x', color='r')
ax.plot(-zs, d_from_zv(zs))
# ax.plot(d_from_zv(zs), zs)
ax.set_xlabel("-z")
ax.set_ylabel("d")
plt.show()

