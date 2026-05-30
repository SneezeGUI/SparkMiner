(ns miner.server
  (:require [clojure.data.json :as json]
            [clojure.core.async
             :as a
             :refer [>! <! >!! <!! go go-loop chan buffer sliding-buffer
                     close! thread alt! alts! alts!! timeout]])
  (:import java.io.InputStreamReader
           [java.net Socket SocketTimeoutException]
           java.io.BufferedReader
           java.io.PrintWriter))


(defn make-sender
  "Creates a function to send data to the supplied socket"
  [socket]
  (let [out-stream (-> socket (.getOutputStream))
        buffered-out (PrintWriter. out-stream true)]
    (fn [payload]
      (.println buffered-out (json/write-str payload)))))


(defn make-receiver
  "Creates a function to read data from the supplied socket"
  [socket]
  (let [in-stream (-> socket
                      (.getInputStream)
                      (InputStreamReader.)
                      (BufferedReader.))]
    (fn []
      (json/read-str (.readLine in-stream)))))


(defn wrap-receiver
  "Wrap the sock-recv channel to
  (a) swallow its timeout exception, and
  (b) put its output onto a channel"
  [sock-recv]
  (let [out (chan)]
    (go-loop []
      (try
        (let [msg (sock-recv)]
          (>! out msg))
        (catch SocketTimeoutException e
          ;; ignore
          ))
      (recur))
    out))


(defn with-timeout
  "apply a short timeout to the socket so
  the miner can be shutdown without a hanging thread"
  [socket duration]
  (.setSoTimeout socket duration)
  socket)


(defn make-server [host port]
  (let [socket (with-timeout (Socket. host port) 1000)
        sock-send (make-sender socket)
        sock-recv (make-receiver socket)
        sock-recv-ch (wrap-receiver sock-recv)
        running (atom true)
        in (chan)
        out (chan)]
    (go-loop []
      (alt! sock-recv-ch ([msg] (>! out msg))
            in ([msg] (sock-send msg)))
      (when running
        (recur)))
    [in out]))

