from flask import Flask, request, send_file
from PIL import Image, ImageFilter
import io

app = Flask(__name__)

@app.route('/process', methods=['POST'])
def process_image():
    # Verifica imaginea
    if 'image' not in request.files:
        return 'No image provided', 400
    image_file = request.files['image']
    op_type = request.form.get('type', 'resize')
    try:
        img = Image.open(image_file.stream)
    except Exception as e:
        return f'Invalid image: {e}', 400
    # Prelucreaza imaginea in functie de tip
    if op_type == 'resize':
        img = img.resize((img.width // 2, img.height // 2))
    elif op_type == 'grayscale':
        img = img.convert('L')
    elif op_type == 'blur':
        img = img.filter(ImageFilter.GaussianBlur(radius=8))
    else:
        return 'Unknown operation', 400
    # Pregateste un raspuns
    buf = io.BytesIO()
    img.save(buf, format='JPEG')
    buf.seek(0)
    return send_file(buf, mimetype='image/jpeg')

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=9000)
