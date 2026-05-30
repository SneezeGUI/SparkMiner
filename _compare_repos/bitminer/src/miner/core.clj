(ns miner.core
  (:require [clojure.core.async
             :as a
             :refer [>! <! >!! <!! go go-loop chan buffer
                     sliding-buffer close! thread alts! alts!! timeout]]
            [clojure.data.json :as json]
            [clojure.math.numeric-tower :as math]
            [clojure.pprint :refer [pprint]]
            [miner.mining :as mining]
            [miner.server :as server]
            [miner.slushpool :as slush])
  (:gen-class))


(defn notify-workers
  "Push the new mining data to workers (atom watcher func)"
  [ch context key old-val new-val]
  (>!! ch {:new-job new-val}))


(defn enqueue-work
  "Push a given job onto a work queue keyed by pdiff value"
  ;; TODO: actually consume from this queue
  [work-pool difficulty new-job]
  (if-not (contains? work-pool difficulty)
    (assoc work-pool difficulty (conj (clojure.lang.PersistentQueue/EMPTY) val))
    (assoc work-pool difficulty (conj (get work-pool difficulty) new-job))))


(defn build-stat-summary
  "Calculates basic stats about the hash-rate of the workers"
  [start-time workers]
  (let [stat-chs (map last workers)
        stats (map <!! stat-chs)
        total-calls (:total-calls (apply merge-with + stats))
        runtime (int (/ (- (System/currentTimeMillis) start-time) 1000))
        hash-per-sec (int (/ total-calls runtime))]
    {:runtime runtime
     :total-hashes total-calls
     :hashes-per-second hash-per-sec}))


;; Specify our final hashing function
(def hashing-func (-> mining/block-hasher
                      (mining/wrap-bigint)
                      (mining/wrap-result)
                      (mining/wrap-metrics)))


;; Populate the workers
(def start-worker (mining/make-worker hashing-func))


(defn start-supervisor
  "Primary control point of the mining operation"
  []
  (let [start-time (System/currentTimeMillis)
        [send-ch recv-ch] (server/make-server "stratum.slushpool.com" 3333)
        msg-id (atom 3)
        subscription (slush/subscribe-client send-ch recv-ch)
        nonce (:nonce subscription)
        nonce-size (:nonce2-size subscription)
        current-difficulty (atom Integer/MAX_VALUE)
        job-pool (atom {})
        active-job (atom {})
        grab-next (atom false)
        num-cores (.availableProcessors (Runtime/getRuntime))
        nonce-offsets (mining/split-nonce-range (mining/max-nonce nonce-size) num-cores)
        workers (map #(start-worker %) nonce-offsets)
        worker-in-chs (map #(nth % 1) workers)]

    ;; attach listeners to notify workers of latest active job
    (doseq [[pid in-ch out stats] workers]
      (add-watch active-job pid (partial notify-workers in-ch)))

    ;; listening for successfully mined blocks from workers
    (go-loop []
      (let [chans (into [] (map #(nth %1 2) workers))
            [payload ch] (alts! chans)]
        (println "Message:: " payload)
        (>! send-ch (slush/submit-msg! payload))
        (swap! msg-id inc)
        (recur)))

    (go-loop []
      (let [msg (<! recv-ch)
            params (slush/process-msg msg)
            method (get msg "method")]

        (when (= method "mining.set_difficulty")
          (when (< (:difficulty params) @current-difficulty)
            ;; Success is getting a share in the pool with our
            ;; super slow cpu miner. As such, we tail the jobs
            ;; which have lower difficulty requirements
            (reset! grab-next true))
          (reset! current-difficulty (:difficulty params)))

        (when (= method "mining.notify")
          (let [this-job (merge params {:nonce nonce :difficulty @current-difficulty})]
            (swap! job-pool
                   enqueue-work
                   @current-difficulty
                   this-job)
            (when (some true? [(:clean params) @grab-next (empty? @active-job)])
              (reset! grab-next false)
              (reset! active-job this-job))))
        (recur)))

    ; show some stats every 20 seconds
    (go-loop []
      (let [_ (<! (timeout 20000))]
        (pprint (build-stat-summary start-time workers)))
      (recur))
    ))


(defn -main
  "I don't do a whole lot ... yet."
  [& args]
  (println "Hello, World!"))