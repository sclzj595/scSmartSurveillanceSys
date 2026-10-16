import cv2
import time

class LeftItemModule:
    def __init__(self, threshold=10, min_area=1000):
        self.bg_subtractor = cv2.createBackgroundSubtractorMOG2()
        self.item_dict = {}  # 位置: [首次检测到时间, (x, y, w, h)]
        self.THRESHOLD = threshold
        self.MIN_AREA = min_area

    def run(self):
        cap = cv2.VideoCapture(0)
        while True:
            ret, frame = cap.read()
            if not ret:
                break
            fg_mask = self.bg_subtractor.apply(frame)
            contours, _ = cv2.findContours(fg_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
            now = time.time()
            current_items = {}
            for cnt in contours:
                if cv2.contourArea(cnt) > self.MIN_AREA:
                    x, y, w, h = cv2.boundingRect(cnt)
                    center = (x + w//2, y + h//2)
                    key = f"{center[0]//20}_{center[1]//20}"
                    if key not in self.item_dict:
                        self.item_dict[key] = [now, (x, y, w, h)]
                    else:
                        self.item_dict[key][1] = (x, y, w, h)
                    current_items[key] = True
            # 检查遗留物品
            for k in list(self.item_dict.keys()):
                appear_time, (x, y, w, h) = self.item_dict[k]
                if k not in current_items:
                    self.item_dict.pop(k)
                    continue
                if now - appear_time > self.THRESHOLD:
                    cv2.rectangle(frame, (x, y), (x+w, y+h), (0,0,255), 2)
                    cv2.putText(frame, 'Left Item', (x, y-10), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0,0,255), 2)
            cv2.imshow('Left Item', frame)
            if cv2.waitKey(1) & 0xFF == 27:
                break
        cap.release()
        cv2.destroyAllWindows()