(ns miner.slushpool
  "Creators and parsers for the SlushPool message format"
  (:require [environ.core :refer [env]]
            [miner.binascii :as binascii]
            [clojure.core.async
             :refer [>! <! >!! <!! ]]))


(def worker-id (env :username))
(def super-secret (env :password))


(defn- make-subscribe-msg []
  {:method "mining.subscribe"
   :params []})


(defn- make-auth-msg [worker-id password]
  {:params [worker-id password]
   :method "mining.authorize"})


(defn- make-submit-msg [worker-id params]
  {:params (cons worker-id params)
   :method "mining.submit"})


(defn- wrap-autoinc-id [f]
  (let [id (atom 1)]
    (fn [& args]
      (let [result (apply f args)
            out (merge result {:id @id})]
        (swap! id inc)
        out))))



(def auth-msg! (wrap-autoinc-id #(make-auth-msg worker-id super-secret)))

(def subscribe-msg! (wrap-autoinc-id make-subscribe-msg))

(def submit-msg! (wrap-autoinc-id (partial make-submit-msg "ckiehl.worker1")))


(defn- parse-subscription-msg
  "Parses the oddly nested mining subscription message
  and returns the relevant fields as a map"
  [msg]
  (let [results (get msg "result")
        [[[_ difficulty] [_ notify]] nonce nonce2-size] results]
    {:set-difficulty (Integer/parseInt difficulty)
     :mining-notify (Integer/parseInt notify)
     :nonce nonce
     :nonce2-size nonce2-size}))


(defn- parse-job-message
  "parse the array of headers returned from a job notification
  and return a map with useful names"
  [msg]
  (let [bindings [:jobid
                  :prev-hash
                  :coinb1
                  :coinb2
                  :merkle-hashes
                  :version
                  :nbits
                  :ntime
                  :clean]]
    (zipmap bindings (get msg "params"))))


(defn- clean-job [job-params]
  (let [version (binascii/swap-endianness (str (:version job-params)))]
    (assoc job-params :version version)))


(defn- parse-difficulty-msg [msg]
  {:difficulty (first (get msg "params"))
   :id (get msg "id")})


(defn process-msg [msg]
  (try
    (case (get msg "method")
      "mining.notify" (clean-job (parse-job-message msg))
      "mining.set_difficulty" (parse-difficulty-msg msg))
    (catch Exception e
      (println "=-=-=-=-=-=-=-=-=-=-")
      (println e)
      (println "msg" msg)
      (println "=-=-=-=-=-=-=-=-=-=-")
      )))


(defn subscribe-client [send-ch recv-ch]
  (>!! send-ch (auth-msg!))
  (<!! recv-ch)
  (>!! send-ch (subscribe-msg!))
  (parse-subscription-msg (<!! recv-ch)))




