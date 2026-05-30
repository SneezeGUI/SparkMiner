(defproject miner "0.1.0-SNAPSHOT"
  :description "FIXME: write description"
  :url "http://example.com/FIXME"
  :license {:name "Eclipse Public License"
            :url "http://www.eclipse.org/legal/epl-v10.html"}
  :dependencies [[digest "1.4.5"]
                 [environ "1.1.0"]
                 [org.clojure/clojure "1.8.0"]
                 [org.clojure/core.async "0.3.443"]
                 [org.clojure/data.json "0.2.6"]
                 [org.clojure/math.numeric-tower "0.0.4"]
                 [http-kit "2.2.0"]]
  :plugins [[lein-environ "1.1.0"]]
  :main ^:skip-aot miner.core
  :target-path "target/%s"
  :profiles {:uberjar {:aot :all}})
