import cv2
from face_recognition_mod import FaceRecModule
from forbidden_zone_mod import ForbiddenZoneModule
from left_item_mod import LeftItemModule

def main():
    print("请选择功能:")
    print("1. 实时人脸检测与识别")
    print("2. 禁区闯入报警")
    print("3. 物品遗留检测")
    choice = input("输入序号: ")
    if choice == '1':
        FaceRecModule().run()
    elif choice == '2':
        ForbiddenZoneModule().run()
    elif choice == '3':
        LeftItemModule().run()
    else:
        print("无效选择")

if __name__ == "__main__":
    main()