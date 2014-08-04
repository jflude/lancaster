angular.module('cachesterApp', [])
  .filter('bytes', function() {
    return function(bytes, precision) {
      if (isNaN(parseFloat(bytes)) || !isFinite(bytes) || bytes == 0) return '0';
      if (typeof precision === 'undefined') precision = 1;
      var units = ['bytes', 'kB', 'MB', 'GB', 'TB', 'PB'],
        number = Math.floor(Math.log(bytes) / Math.log(1024));
      return (bytes / Math.pow(1024, Math.floor(number))).toFixed(precision) +  ' ' + units[number];
    }
  })
  .controller('CachesterController', ['$scope','$http', function($scope, $http) {
    $scope.host = "";
    $scope.lastError = "";
    $scope.addHost = function() {
      $http.get("/add?host="+$scope.host).success(function(){
        console.log("Added:",$scope.host);
        $scope.host = "";
        $scope.lastError = "";
      }).error(function(data){
        $scope.lastError = data
      });
    };
  }])
  .controller('StatusController', ['$scope', '$http', '$timeout',function($scope, $http, $timeout) {
    $scope.alive = {};
    $scope.dead = {};
    $scope.showDead = true;
    $scope.removeHost = function(host){
      $http.get("/remove?host="+host).then(function(r){
        console.log("Remove",r.status,r.data);
      });
      delete $scope.alive[host];
      delete $scope.dead[host];
      $scope.host = "";
    }
    var update = function(src, dest){
      for (var k in src){
        dest[k]=src[k];
      }
      for (var k in dest){
        if (!src[k])
          delete dest[k];
      }
    };
    var updateStatus = function() {
      $http.get("/status").success(function(data,status){
        if (status == 200){
          var a = {}
          var d = {}
          for (var k in data){
            if (data[k].Alive){
              a[k] = data[k];
            } else {
              d[k] = data[k];
            }
            // $scope.status[k] = data[k]
          }
          update(a,$scope.alive);
          update(d,$scope.dead);
          // $scope.showDead = $scope.dead.length > 0;
          // console.log($scope.dead,$scope.dead.size, $scope.showDead)
          // for (var k in $scope.status){

          //   if (!data[k])
          //     delete $scope.status[k]
          // }
          
        } else {
          console.log(status,data)
        }
        $timeout(updateStatus,1000);
      }).error(function(data,status){
        console.log(status,data)
        $timeout(updateStatus,1000);
      });
    };
    updateStatus();
  }]);
