#!/usr/bin/env python3


def password_cksum(password: str):
    cksum = 0
    for i in range(min(len(password), 20)):
        cksum += ord(password[i]) * (i + i // 5 + 1)
    return cksum


def printable(i: int | float):
    return 0x21 <= i <= 0x7e


weight = [i + i // 5 + 1 for i in range(20)]
total_weight = [sum(weight[:i]) for i in range(len(weight) + 1)]
lower = [0x21 * total_weight[i + 1] for i in range(len(weight))]
upper = [0x7e * total_weight[i + 1] for i in range(len(weight))]

print(weight)
print(total_weight)
print(lower)
print(upper)

password = 'pmlxzjtlx'
cksum = password_cksum(password)
print(password, cksum)

for i in range(20):
    print(f'{i:2d}', end=' ')
    for j in range(0x21, 0x7e + 1):
        print('T' if lower[i-1] <= cksum - j *
              weight[i] <= upper[i-1] else ' ', end='')
    print()
