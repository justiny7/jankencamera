from PIL import Image
import numpy as np

with open("out.txt", "w") as f:
    for filename in ["0.jpg", "1.jpg", "2.jpg"]:
        img = Image.open(filename).convert("RGB")
        arr = np.array(img)

        print(arr.shape)

        values = []

        for pixel in arr.reshape(-1, 3):
            r, g, b = pixel
            values.extend([r, g, b])

        f.write(" ".join(map(str, values)))
        f.write("\n")
