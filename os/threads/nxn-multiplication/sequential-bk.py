import time
import random

# rows1, cols1 = 100, 200
# rows2, cols2 = 200, 150   # nxm * mxn

# mat1 = [[random.randint(1, 10) for _ in range(cols1)] for _ in range(rows1)]
# mat2 = [[random.randint(1, 10) for _ in range(cols2)] for _ in range(rows2)]

mat1=[
    [1,2,3],
    [4,5,6],
]
mat2=[
    [1,2],
    [3,4],
    [5,6]
]

start = time.time()

new=[]
# print(mat1[1])
for i in range(len(mat1)): #no of rows in mat1
    new_rows = []
    for j in range(len(mat2[0])): #no of elements in mat2 row
        total = 0
        for k in range(len(mat2)): #no of rows in mat2 
            total += mat1[i][k] * mat2[k][j]
        new_rows.append(total)
    new.append(new_rows)

end = time.time()

# print(new)
# print(len(mat2[0]))

print("Total time for sequential execution",round(end - start, 5), "seconds")

print(f"{end - start:.6f} seconds")