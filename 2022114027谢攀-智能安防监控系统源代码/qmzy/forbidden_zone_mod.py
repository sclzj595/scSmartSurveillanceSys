import cv2

class ForbiddenZoneModule:
    def __init__(self):
        self.forbidden_zone = []
        self.drawing = False

    def draw_rectangle(self, event, x, y, flags, param):
        if event == cv2.EVENT_LBUTTONDOWN:
            self.forbidden_zone = [(x, y)]
            self.drawing = True
        elif event == cv2.EVENT_MOUSEMOVE and self.drawing:
            self.forbidden_zone[1:] = [(x, y)]
        elif event == cv2.EVENT_LBUTTONUP:
            self.forbidden_zone.append((x, y))
            self.drawing = False

    def is_in_zone(self, pt):
        if len(self.forbidden_zone) != 2:
            return False
        (x1, y1), (x2, y2) = self.forbidden_zone
        x1, x2 = sorted([x1, x2])
        y1, y2 = sorted([y1, y2])
        return x1 <= pt[0] <= x2 and y1 <= pt[1] <= y2

    def run(self):
        cap = cv2.VideoCapture(0)
        cv2.namedWindow('Zone')
        cv2.setMouseCallback('Zone', self.draw_rectangle)
        face_cascade = cv2.CascadeClassifier(cv2.data.haarcascades + 'haarcascade_frontalface_default.xml')
        while True:
            ret, frame = cap.read()
            if not ret:
                break
            if len(self.forbidden_zone) == 2:
                cv2.rectangle(frame, self.forbidden_zone[0], self.forbidden_zone[1], (0,0,255), 2)
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            faces = face_cascade.detectMultiScale(gray, 1.2, 4)
            for (x, y, w, h) in faces:
                center = (x + w//2, y + h//2)
                color = (0,255,0)
                if self.is_in_zone(center):
                    color = (0,0,255)
                    cv2.putText(frame, 'ALARM: Forbidden Zone!', (20,50), cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0,0,255), 3)
                cv2.rectangle(frame, (x,y), (x+w,y+h), color, 2)
            cv2.imshow('Zone', frame)
            if cv2.waitKey(1) & 0xFF == 27:
                break
        cap.release()
        cv2.destroyAllWindows()